#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/un.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "functions.c"

#define UNIX_PATH_MAX 108
#define SOCKNAME "./mysock"
#define N 100
#define ERROR_CHECK(msg, func) \
	if (func == -1)            \
	{                          \
		perror(msg);           \
		exit(EXIT_FAILURE);    \
	}

int main(int argc, char **argv)
{
	int fileDescriptorSocket, connectionSocketFileDescriptor;
	unlink(SOCKNAME);
	struct sockaddr_un sa = {
        .sun_family = AF_UNIX,
		.sun_path = SOCKNAME,
    };
	
	char buf[N];

	if (fork() != 0) //* Padre Server
	{
		fileDescriptorSocket = socket(AF_UNIX, SOCK_STREAM, 0);
		//ERROR_CHECK("setsockopt", setsockopt(fileDescriptorSocket, SOL_SOCKET, SO_REUSEADDR, (void*)1, sizeof((void*)1)));
		ERROR_CHECK("bind", bind(fileDescriptorSocket, (struct sockaddr *)&sa, sizeof(sa)));
		ERROR_CHECK("listen", listen(fileDescriptorSocket, SOMAXCONN));
		printf("Waiting for client connection...\n");
		connectionSocketFileDescriptor = accept(fileDescriptorSocket, NULL, 0);
		ERROR_CHECK("accept", connectionSocketFileDescriptor);
		printf("Accepted a client connection.\n");
		read(connectionSocketFileDescriptor, buf, N);
		fprintf(stderr, "Server got: %s\n", buf);
		write(connectionSocketFileDescriptor, "Bye !", 5);
		close(fileDescriptorSocket);
		close(connectionSocketFileDescriptor);
		exit(EXIT_SUCCESS);
	}
	else
	{ /* figlio, client */
		fileDescriptorSocket = socket(AF_UNIX, SOCK_STREAM, 0);
		while (connect(fileDescriptorSocket, (struct sockaddr *)&sa, sizeof(sa)) == -1)
		{
			if (errno == ENOENT)
				sleep(1); /* sock non esiste */
			else
			{
				perror("connect");
				exit(EXIT_FAILURE);
			}
		}
		write(fileDescriptorSocket, "Hallo !", 7);
		read(fileDescriptorSocket, buf, N);
		fprintf(stderr, "Client got: %s\n", buf);
		close(fileDescriptorSocket);
		exit(EXIT_SUCCESS);
	}
	return 0;
}