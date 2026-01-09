#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#define port 8080
#define ip_addr inaddr_loopback
// inaddr_loopback is localhost

int main() {
	int fd = socket(af_inet, sock_stream, 0);

	printf("begin to initialize... ");
	// so_reuseport <- can run many nginy instance on same port
	int opt = 1;
	if (setsockopt(fd, sol_socket, so_reuseport, &opt, sizeof(opt)) < 0) {
		perror("socket option failure");
		exit(exit_failure);
	}

	struct sockaddr_in addr;
	addr.sin_family = af_inet; 		// ipv4
	addr.sin_port = htons(port); 
	addr.sin_addr.s_addr = htonl(ip_addr);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind failed");
		exit(exit_failure);
	}

	// max 511 clients waiting for connection
	if (listen(fd, 511) < 0) {
		perror("listen failed");
		exit(exit_failure);
	}

	printf("successful.\nlistening to %s:%d.\n", inet_ntoa(addr.sin_addr), port);

	while (1) {
		int client_fd = accept(fd, null, null);
		
		char buffer[1024] = {0};
		read(client_fd, buffer, 1024 - 1);

		char method[16] = {0};
		char path[2048] = {0};
		
		sscanf(buffer, "%s %s", method, path);

		if (strcmp(path, "/") == 0) {
			strcpy(path, "/index.html");
		}

		file *file = fopen(path + 1, "r");
		if (file == null) {
			char *error_response = "http/1.1 404 not found\n\nfile not found!";
			write(client_fd, error_response, strlen(error_response));
		} else {
			char file_buffer[1024] = {0};
			int bytes_read = fread(file_buffer, 1, 1024, file);
			char header[1024] = {0};
			sprintf(header,
				"http/1.1 200 ok\n"
				"content-type: text/html\n"
				"content-length: %d\n"
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
