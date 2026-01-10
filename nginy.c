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
#include <signal.h>
#define PORT 8080
#define IP_ADDR INADDR_LOOPBACK

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

    char request[8192];
    char response_buffer[1024];
} client_t;

void free_client(client_t *client) {
    if (client == NULL) return;
    if (client->client_fd > 0) close(client->client_fd);
    if (client->file_fd > 0) close(client->file_fd);
    free(client);
}


int main() {
    return 0;
}