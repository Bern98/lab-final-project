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
#include <pthread.h>
#include <fcntl.h>
#include "functions.h"

int fileDescriptorSocket, connectionSocketFileDescriptor;
struct sockaddr_un sa = {
	.sun_family = AF_UNIX,
	.sun_path = SOCKNAME,
};

int main(int argc, char **argv)
{
	unlink(SOCKNAME);
	char buf[N];

	if (fork() != 0) //* Padre Server
	{
		fileDescriptorSocket = socket(AF_UNIX, SOCK_STREAM, 0);
		ERROR_CHECK("bind", bind(fileDescriptorSocket, (struct sockaddr *)&sa, sizeof(sa)), -1);
		ERROR_CHECK("listen", listen(fileDescriptorSocket, SOMAXCONN), -1);
		printf("Waiting for client connection...\n");
		connectionSocketFileDescriptor = accept(fileDescriptorSocket, NULL, 0);
		ERROR_CHECK("accept", connectionSocketFileDescriptor, -1);
		printf("Accepted a client connection.\n");
		//*-------------------------

		int n_of_threads = atoi(argv[1]);
		char *dirname = argv[2];
		if (argc < 3)
			return EXIT_FAILURE;

		ERROR_CHECK("chdr", chdir(dirname), -1);

		pthread_t mainT, threads_array[n_of_threads];
		ThreadArgs ThreadArgs;
		ThreadArgs.dirname = malloc(sizeof(char) * 100);
		strcpy(ThreadArgs.dirname, dirname);
		ThreadArgs.n_of_threads = n_of_threads;

		ThreadArgs.queue = malloc(sizeof(Queue));
		init_queue(ThreadArgs.queue);

		if (pthread_create(&mainT, NULL, &mainThreadFunction, &ThreadArgs) != 0)
		{
			perror("main pthread_create");
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < n_of_threads; i++)
		{
			if (pthread_create(&threads_array[i], NULL, &workerThreadPrint, &ThreadArgs) != 0)
			{
				perror("pthread_create");
				exit(EXIT_FAILURE);
			}
			// printf("thread %d creato\n", i + 1);
		}

		if (pthread_join(mainT, NULL) != 0)
		{
			perror("pthread_join");
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < n_of_threads; i++)
		{
			if (pthread_join(threads_array[i], NULL) != 0)
			{
				perror("pthread_join");
				exit(EXIT_FAILURE);
			}
			// printf("thread %d terminato\n", i + 1);
		}
		queue_destroy(ThreadArgs.queue);

		//*-------------------------
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
			if (errno == ENOENT || errno == ECONNREFUSED)
				sleep(1); /* sock non esiste o connessione rifiutata */
			else
			{
				perror("connect");
				exit(EXIT_FAILURE);
			}
		}
		write(fileDescriptorSocket, "Hallo !", 7);
		read(fileDescriptorSocket, buf, N);
		// fprintf(stderr, "Client got: %s\n", buf);
		close(fileDescriptorSocket);
		exit(EXIT_SUCCESS);
	}
	free(buf);
	return 0;
}