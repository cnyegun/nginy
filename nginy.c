#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#define port 8080
#define IP_ADDR INADDR_LOOPBACK
// INADDR_LOOPBACK is localhost

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

		if (strcmp(path, "/") == 0) {
			strcpy(path, "/index.html");
		}

		file *file = fopen(path + 1, "r");
		if (file == NULL) {
			char *error_response = "HTTP/1.1 404 Not Found\n\nFile Not Found!";
			write(client_fd, error_response, strlen(error_response));
		} else {
			char file_buffer[1024] = {0};
			int bytes_read = fread(file_buffer, 1, 1024, file);
			char header[1024] = {0};
			sprintf(header,
				"HTTP/1.1 200 OK\n"
				"Content-Type: text/html\n"
				"Content-Length: %d\n"
				"\n",
				bytes_read
			);
			write(client_fd, header, strlen(header));
			write(client_fd, file_buffer, bytes_read);

			fclose(file);
		}
		close(client_fd);
	}

	return 0;
}
