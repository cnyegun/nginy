#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#define PORT 8080
#define IP_ADDR INADDR_LOOPBACK
// INADDR_LOOPBACK is localhost

const char *get_file_type(const char *path) {
	const char *ext = strrchr(path, '.');

	if (!ext) return "text/plain";

	if (strcmp(ext, ".html") == 0) return "text/html";
	if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
	if (strcmp(ext, ".png") == 0) return "image/png";
	if (strcmp(ext, ".css") == 0) return "text/css";
	if (strcmp(ext, ".js") == 0) return "application/javascript";

	return "text/plain";
}

int main() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	printf("Begin to initialize... ");
	// SO_REUSEPORT <- can run many nginy instance on same port
	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
		perror("Socket option failure");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET; 		// IPv4
	addr.sin_port = htons(PORT); 
	addr.sin_addr.s_addr = htonl(IP_ADDR);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("Bind failed");
		exit(EXIT_FAILURE);
	}

	// max 511 clients waiting for connection
	if (listen(fd, 511) < 0) {
		perror("Listen failed");
		exit(EXIT_FAILURE);
	}

	printf("Successful.\nListening to %s:%d.\n", inet_ntoa(addr.sin_addr), PORT);

	while (1) {
		int client_fd = accept(fd, NULL, NULL);
		
		char buffer[1024] = {0};
		read(client_fd, buffer, 1024 - 1);

		char method[16] = {0};
		char path[2048] = {0};
		
		sscanf(buffer, "%s %s", method, path);
		printf("Method: %s, Path: %s\n", method, path);

		// Sanitize the path. It only allowed to read inside public/
		if (strstr(path, "..")) {
			char *error = "HTTP/1.1 403 Forbidden\n\nAccess Denied!";
			write(client_fd, error, strlen(error));
			close(client_fd);
			continue;
		}

		if (strcmp(path, "/") == 0) {
			strcpy(path, "/index.html");
		}

		char file_path[2048] = {0};
		sprintf(file_path, "public%s", path);

		FILE *file = fopen(file_path, "r");
		if (file == NULL) {
			char *error_response = "HTTP/1.1 404 Not Found\n\nFile Not Found!";
			write(client_fd, error_response, strlen(error_response));
		} else {
			// Get the file size
			fseek(file, 0, SEEK_END);
			long fsize = ftell(file);
			fseek(file, 0, SEEK_SET);

			// Send the header first
			const char *content_type = get_file_type(file_path);
			char header[1024] = {0};
			sprintf(header,
				"HTTP/1.1 200 OK\n"
				"Content-Type: %s\n"
				"Content-Length: %ld\n"
				"\n",
				content_type, fsize
			);
			write(client_fd, header, strlen(header));

			char file_buffer[4096] = {0};
			int bytes_read;
			while ((bytes_read = fread(file_buffer, 1, 4096, file)) > 0) {
				write(client_fd, file_buffer, bytes_read);
			}

			fclose(file);
		}
		close(client_fd);
	}

	return 0;
}
