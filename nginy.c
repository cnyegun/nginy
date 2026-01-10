#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#define PORT 8080
#define IP_ADDR INADDR_LOOPBACK
#define BACKLOG 511
#define MAX_EVENTS 64
#define REQUEST_BUFF_LEN 8192
#define RESPONSE_BUFF_LEN 1024
#define PUBLIC_DIR "public"

typedef enum {
    STATE_READ_REQUEST, 
    STATE_SEND_HEADER, 
    STATE_SEND_FILE 
} state_t;

typedef struct {
    off_t file_offset;          // Position on the file so we can continue sending
    size_t request_offset;      // Requests bytes readed so far
    size_t content_length;      // Size of the response

    int client_fd;
    int file_fd;
    state_t state;

    uint32_t header_len;        // Length of the header
    uint32_t header_sent;       // Header bytes we sent so far

    char request[REQUEST_BUFF_LEN];
    char response_buffer[RESPONSE_BUFF_LEN];
} client_t;

const char *get_mime_type(const char *path);
void free_client(client_t *client);
int setup_server_socket();
void handle_new_connection(int epoll_fd, int server_fd);
void handle_client_event(int epoll_fd, client_t *c);
void handle_state_read(int epoll_fd, client_t *c);
void handle_state_send_header(client_t *c);
void handle_state_send_file(client_t *c);

int main() {
    signal(SIGPIPE, SIG_IGN);
    int server_fd = setup_server_socket();

    // Init epoll
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("Epoll create failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Create an epoll event
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;

    // Hand it to the kernel
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("Epoll ctl failed");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    printf("NginY listening on port %d...\n", PORT);

    // Event loop 

    struct epoll_event events[MAX_EVENTS] = {0};

    while (1) {
        int n_fd_ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_fd_ready < 0) {
            perror("Epoll wait failed\n");
            close(server_fd);
            close(epoll_fd);
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < n_fd_ready; i++) {
            if (events[i].data.ptr == NULL) {
                handle_new_connection(epoll_fd, server_fd);
            } else {
                client_t *client = (client_t *)events[i].data.ptr;
                handle_client_event(epoll_fd, client);
            }
        }
    }
    return 0;
}

const char *get_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".css") == 0)  return "text/css";
    if (strcmp(dot, ".js") == 0)   return "application/javascript";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    if (strcmp(dot, ".gif") == 0)  return "image/gif";
    
    return "text/plain";
}

void free_client(client_t *client) {
    if (client == NULL) return;
    if (client->client_fd > 0) close(client->client_fd);
    if (client->file_fd > 0) close(client->file_fd);
    free(client);
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void send_404(client_t *c) {
    const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\nConnection: close\r\n\r\n404 Not Found";
    write(c->client_fd, response, strlen(response));
    free_client(c);
}

void send_403(client_t *c) {
    const char *response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 13\r\nConnection: close\r\n\r\n403 Forbidden";
    write(c->client_fd, response, strlen(response));
    free_client(c);
}

int prepare_file_response(client_t *c) {
    // Lets parse the header;
    char method[16] = {0};
    char path[512] = {0};
    if (sscanf(c->request, "%15s %511s", method, path) != 2) {
        free_client(c);
        return -1;
    }
    // Safety check
    if (strstr(path, "..")) {
        send_403(c);
        // Send 403 also free the client
        return -1;
    }

    if (strcmp(path, "/") == 0) 
        strcpy(path, "/index.html");
    
    char file_path[1024];
    struct stat file_stat;
    snprintf(file_path, sizeof(file_path), "%s%s", PUBLIC_DIR, path);
    const char *mime_type = get_mime_type(file_path);

    if (stat(file_path, &file_stat) < 0 
    || (c->file_fd = open(file_path, O_RDONLY)) < 0) {
        send_404(c);
        // Send 404 also call free_client
        return -1;
    }

    c->content_length = file_stat.st_size;
    snprintf(
        c->response_buffer,
        sizeof(c->response_buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        mime_type,
        c->content_length
    );

    c->header_len = strlen(c->response_buffer);
    c->header_sent = 0;
    c->state = STATE_SEND_HEADER;
    return 0;
}

// Returns server_fd if sucess, crash if failed
int setup_server_socket() {

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
		exit(EXIT_FAILURE);
    }

    // Configure socket for reusable address/port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
		perror("Setsockopt failed");
        close(server_fd);
		exit(EXIT_FAILURE);
	}

    // Bind
    struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT); 
	addr.sin_addr.s_addr = htonl(IP_ADDR);

	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("Bind failed");
        close(server_fd);
		exit(EXIT_FAILURE);
	}

    // Listen
	if (listen(server_fd, BACKLOG) < 0) {
		perror("Listen failed");
        close(server_fd);
		exit(EXIT_FAILURE);
	}

    // Non-blocking for epoll
    if (set_nonblocking(server_fd) < 0) {
        perror("Set nonblocking failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    return server_fd;
}

void handle_new_connection(int epoll_fd, int server_fd) {
    // Accept new connection so we have a new client_fd
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(
        server_fd, 
        (struct sockaddr *)&client_addr, 
        &client_len);

    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        perror("Accept failed");
        close(client_fd);
        return;
    }
    if (set_nonblocking(client_fd) < 0) {
        perror("Set nonblocking failed");
        close(client_fd);
        return;
    }
    // Create a client_t
    client_t *client = calloc(1, sizeof *client);
    if (!client) {
        perror("Memory allocation failed");
        close(client_fd);
        return;
    }
    // Init the new client
    client->client_fd = client_fd;
    client->state = STATE_READ_REQUEST;
    client->file_fd = -1;

    struct epoll_event client_ev;
    client_ev.events = EPOLLIN | EPOLLET;
    client_ev.data.ptr = client;

    // Hand it to the kernel
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
        perror("Epoll ctl failed");
        close(client_fd);
        free(client);
        return;
    }
}

void handle_state_read(int epoll_fd, client_t *c) {
    ssize_t bytes_read = read(
        c->client_fd, 
        c->request + c->request_offset,
        REQUEST_BUFF_LEN - c->request_offset - 1
    );
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        else {
            perror("Read request failed");
            free_client(c);
            return;
        }
    } else if (bytes_read == 0) {
        // The client hung up.
        free_client(c);
        return;
    }
    assert(bytes_read > 0);
    c->request_offset += bytes_read;
    c->request[c->request_offset] = '\0';

    if (strstr(c->request, "\r\n\r\n")) {
        if (prepare_file_response(c) < 0) {
            perror("Prepare file response failed");
            // prepare_file_response takes ownership to free()
            return;
        }
        // Change to write mode
        struct epoll_event ev;
        ev.events = EPOLLOUT | EPOLLET;
        ev.data.ptr = c;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, c->client_fd, &ev) < 0) {
            perror("Epoll_ctl failed");
            free_client(c);
            return;
        }
        handle_state_send_header(c);
    }
}

void handle_state_send_header(client_t *c) {
    ssize_t bytes_sent = write(
        c->client_fd,
        c->response_buffer + c->header_sent, 
        c->header_len - c->header_sent
    );
    
    if (bytes_sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;

        perror("Write header failed");
        free_client(c);
        return;
    }

    c->header_sent += bytes_sent;

    if (c->header_sent == c->header_len) {
        c->state = STATE_SEND_FILE;
        handle_state_send_file(c);
    }
}

void handle_state_send_file(client_t *c) {
    ssize_t bytes_sent = sendfile(
        c->client_fd, 
        c->file_fd, 
        &c->file_offset, 
        c->content_length - c->file_offset
    );

    if (bytes_sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;

        perror("Send file failed");
        free_client(c);
        return;
    }

    if ((size_t)c->file_offset == c->content_length) {
        free_client(c);
    }
}

void handle_client_event(int epoll_fd, client_t *c) {
    if (!c) return;

    switch (c->state) {

        case STATE_READ_REQUEST:
            handle_state_read(epoll_fd, c);
            return;
        case STATE_SEND_HEADER:
            handle_state_send_header(c);
            return;
        case STATE_SEND_FILE:
            handle_state_send_file(c);
            return;
    }
}