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
#include <dirent.h>
#include <math.h>
#include <poll.h>
#include "unboundedqueue.h"
#include "errmsg.h"
#include "sfiles.h"

#define STOP "STOP"
#define TRUE 1
#define FALSE 0
#define N 100
#define SOCKET_IP "127.0.0.1"

typedef struct
{
	Queue *queue;
	char *dirname;
	int n_of_threads;
} ThreadArgs;

typedef struct
{
	int n;
	double avg;
	double std;
	char *filename;
} WorkerResults;

struct sockaddr_in sa;
struct pollfd fds[100];

static void read_and_calculate(char *filename, WorkerResults *resultsStruct);
static void recursive_unfold_and_push(char *, Queue *);
static void *main_thread_function(void *);
static void *worker_thread_function(void *);
static int collector_recieve_from_thread();
static int thread_send_to_collector();
static char buf[N];

int main(int argc, char **argv)
{
	memset(&buf, 0, sizeof(char) * N);
	sa.sin_addr.s_addr = inet_addr(SOCKET_IP);
	sa.sin_port = htons(42000);
	sa.sin_family = AF_INET;

	memset(fds, 0, sizeof(fds));

	if (fork() != 0) //* Padre Server
	{
		if (collector_recieve_from_thread() == -1)
		{
			perror("collector_recieve_from_thread");
			exit(EXIT_FAILURE);
		}
		exit(EXIT_SUCCESS);
	}
	else
	{ /* figlio, client */
		int n_of_threads = atoi(argv[1]);
		char *dirname = argv[2];
		if (argc < 3)
			return EXIT_FAILURE;

		chdir(dirname);
		die_if_error("chdr", -1); // Cambiare directory per far funzionare Recursive Unfold

		pthread_t mainT, threads_array[n_of_threads];
		ThreadArgs ThreadArgs;
		ThreadArgs.dirname = malloc(sizeof(char) * 100);
		strcpy(ThreadArgs.dirname, dirname);
		ThreadArgs.n_of_threads = n_of_threads;
		ThreadArgs.queue = malloc(sizeof(Queue));
		init_queue(ThreadArgs.queue);

		if (pthread_create(&mainT, NULL, &main_thread_function, &ThreadArgs) != 0)
		{
			perror("main pthread_create");
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < n_of_threads; i++)
		{
			if (pthread_create(&threads_array[i], NULL, &worker_thread_function, &ThreadArgs) != 0)
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

		exit(EXIT_SUCCESS);
	}
	free(buf);
	return 0;
}

void recursive_unfold_and_push(char *dirPath, Queue *q)
{
	DIR *dir;
	if ((dir = opendir(dirPath)) == NULL)
	{
		perror("Error opening directory.\n");
		exit(1);
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		char entryPath[1000];
		snprintf(entryPath, sizeof(entryPath), "%s/%s", dirPath, entry->d_name);

		if (entryPath == NULL)
			exit(1);
		if (entry->d_type == 4) // DIR
			recursive_unfold_and_push(entryPath, q);
		else
		{
			if (entry->d_type == 8 && strcmp(get_file_extension(entry->d_name), ".dat") == 0) // REG
				push(strdup(entryPath), q);
		}
	}
	closedir(dir);
	die_if_error("closedir", -1);
}

void read_and_calculate(char *filename, WorkerResults *resultsStruct)
{
	int n = 0;
	double sum = 0, sumSq = 0, avg, variance, std;
	char *data, *token;
	FILE *fp;
	fp = fopen(filename, "r");

	if (fp == NULL)
	{
		perror("Error opening file");
		return;
	}

	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp); // Calcolo file size
	fseek(fp, 0, SEEK_SET);

	data = malloc(fsize + 1);
	fread(data, fsize, 1, fp);
	fclose(fp);

	data[fsize] = '\0';

	// Calculate the sum and sum of squares
	token = strtok(data, " \t\n");
	while (token != NULL)
	{
		double num = atof(token);
		sum += num;
		sumSq += num * num;
		n++;
		token = strtok(NULL, " \t\n");
	}

	// Calculate the mean and variance
	avg = sum / n;
	variance = (sumSq / n) - (avg * avg);

	// Calculate the standard deviation
	std = sqrt(variance);

	// printf("%d\t%.2f\t%.2f\t%s\n", n, avg, std, filename);
	resultsStruct->n = n;
	resultsStruct->avg = avg;
	resultsStruct->std = std;
	strcpy(resultsStruct->filename, filename);

	free(data);
}

void *main_thread_function(void *args)
{
	ThreadArgs *Args = (ThreadArgs *)args;
	Queue *q = Args->queue;
	int n_of_threads = Args->n_of_threads;

	recursive_unfold_and_push(".", q);
	for (int i = 0; i < n_of_threads; i++)
		push((void *)STOP, q);

	pthread_exit(NULL);
}

void *worker_thread_function(void *args)
{
	ThreadArgs *Args = (ThreadArgs *)args;
	Queue *q = Args->queue;
	WorkerResults workerResults;
	workerResults.filename = malloc(sizeof(char) * 150);

	int server_fd = socket(AF_INET, SOCK_STREAM, 0), c = 2; // dopo c volte smette di provare a connettersi

	while (connect(server_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
	{
		if (errno == ENOENT || errno == ECONNREFUSED)
		{
			printf("Client connecting...\n");
			sleep(2);
		}
		else
		{
			perror("connect");
			pthread_exit(NULL);
		}
		c--;
		if (c == 0)
		{
			printf("Time finished...\n");
			pthread_exit(NULL);
		}
	}
	printf("Client connected.\n");

	// while (1)
	//{
	char *poppedDatum = (char *)pop(q);
	if (poppedDatum == NULL || strcmp(poppedDatum, STOP) == 0)
		pthread_exit(NULL);
	else
	{
		// read_and_calculate(poppedDatum, &workerResults);
		printf("Sending: hello\n");
		write(server_fd, "hello", 6);
		//}
		// printf("%d\t%.2f\t%.2f\t%s\n", workerResults.n, workerResults.avg, workerResults.std, workerResults.filename);
		//  printf("[%ld]: %s\n", (long)pthread_self(), poppedDatum);
		free(poppedDatum);
	}

	pthread_exit(NULL);
}

//--------------------------Socket--------------------------------

int collector_recieve_from_thread()
{
	int len, rc, on = 1;
	int listen_sd = -1, new_sd = -1;
	int desc_ready, end_server = FALSE, compress_array = FALSE;
	int close_conn;
	char buffer[80];
	struct sockaddr_in6 addr;
	int timeout;
	struct pollfd fds[200];
	int nfds = 1, current_size = 0, i, j;

	/*************************************************************/
	/* Create an AF_INET6 stream socket to receive incoming      */
	/* connections on                                            */
	/*************************************************************/
	listen_sd = socket(AF_INET6, SOCK_STREAM, 0);
	if (listen_sd < 0)
	{
		perror("socket() failed");
		exit(-1);
	}

	/*************************************************************/
	/* Bind the socket                                           */
	/*************************************************************/
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	memcpy(&addr.sin6_addr, &in6addr_any, sizeof(in6addr_any));
	addr.sin6_port = htons(42000);
	rc = bind(listen_sd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0)
	{
		perror("bind() failed");
		close(listen_sd);
		exit(-1);
	}

	/*************************************************************/
	/* Set the listen back log                                   */
	/*************************************************************/
	rc = listen(listen_sd, 32);
	if (rc < 0)
	{
		perror("listen() failed");
		close(listen_sd);
		exit(-1);
	}

	/*************************************************************/
	/* Initialize the pollfd structure                           */
	/*************************************************************/
	memset(fds, 0, sizeof(fds));

	/*************************************************************/
	/* Set up the initial listening socket                        */
	/*************************************************************/
	fds[0].fd = listen_sd;
	fds[0].events = POLLIN;
	/*************************************************************/
	/* Initialize the timeout to 3 minutes. If no                */
	/* activity after 3 minutes this program will end.           */
	/* timeout value is based on milliseconds.                   */
	/*************************************************************/
	timeout = (3 * 60 * 1000);

	/*************************************************************/
	/* Loop waiting for incoming connects or for incoming data   */
	/* on any of the connected sockets.                          */
	/*************************************************************/
	do
	{
		/***********************************************************/
		/* Call poll() and wait 3 minutes for it to complete.      */
		/***********************************************************/
		printf("Waiting on poll()...\n");
		rc = poll(fds, nfds, timeout);

		/***********************************************************/
		/* Check to see if the poll call failed.                   */
		/***********************************************************/
		if (rc < 0)
		{
			perror("  poll() failed");
			break;
		}

		/***********************************************************/
		/* Check to see if the 3 minute time out expired.          */
		/***********************************************************/
		if (rc == 0)
		{
			printf("  poll() timed out.  End program.\n");
			break;
		}

		/***********************************************************/
		/* One or more descriptors are readable.  Need to          */
		/* determine which ones they are.                          */
		/***********************************************************/
		current_size = nfds;
		for (i = 0; i < current_size; i++)
		{
			/*********************************************************/
			/* Loop through to find the descriptors that returned    */
			/* POLLIN and determine whether it's the listening       */
			/* or the active connection.                             */
			/*********************************************************/
			if (fds[i].revents == 0)
				continue;

			/*********************************************************/
			/* If revents is not POLLIN, it's an unexpected result,  */
			/* log and end the server.                               */
			/*********************************************************/
			if (fds[i].revents != POLLIN)
			{
				printf("  Error! revents = %d\n", fds[i].revents);
				end_server = TRUE;
				break;
			}
			if (fds[i].fd == listen_sd)
			{
				/*******************************************************/
				/* Listening descriptor is readable.                   */
				/*******************************************************/
				printf("  Listening socket is readable\n");

				/*******************************************************/
				/* Accept all incoming connections that are            */
				/* queued up on the listening socket before we         */
				/* loop back and call poll again.                      */
				/*******************************************************/
				do
				{
					/*****************************************************/
					/* Accept each incoming connection. If               */
					/* accept fails with EWOULDBLOCK, then we            */
					/* have accepted all of them. Any other              */
					/* failure on accept will cause us to end the        */
					/* server.                                           */
					/*****************************************************/
					new_sd = accept(listen_sd, NULL, NULL);
					if (new_sd < 0)
					{
						if (errno != EWOULDBLOCK)
						{
							perror("  accept() failed");
							end_server = TRUE;
						}
						break;
					}

					/*****************************************************/
					/* Add the new incoming connection to the            */
					/* pollfd structure                                  */
					/*****************************************************/
					printf("  New incoming connection - %d\n", new_sd);
					fds[nfds].fd = new_sd;
					fds[nfds].events = POLLIN;
					nfds++;

					/*****************************************************/
					/* Loop back up and accept another incoming          */
					/* connection                                        */
					/*****************************************************/
				} while (new_sd != -1);
			}

			/*********************************************************/
			/* This is not the listening socket, therefore an        */
			/* existing connection must be readable                  */
			/*********************************************************/

			else
			{
				printf("  Descriptor %d is readable\n", fds[i].fd);
				close_conn = FALSE;
				/*******************************************************/
				/* Receive all incoming data on this socket            */
				/* before we loop back and call poll again.            */
				/*******************************************************/

				do
				{
					/*****************************************************/
					/* Receive data on this connection until the         */
					/* recv fails with EWOULDBLOCK. If any other         */
					/* failure occurs, we will close the                 */
					/* connection.                                       */
					/*****************************************************/
					rc = recv(fds[i].fd, buffer, sizeof(buffer), 0);
					if (rc < 0)
					{
						if (errno != EWOULDBLOCK)
						{
							perror("  recv() failed");
							close_conn = TRUE;
						}
						break;
					}

					/*****************************************************/
					/* Check to see if the connection has been           */
					/* closed by the client                              */
					/*****************************************************/
					if (rc == 0)
					{
						printf("  Connection closed\n");
						close_conn = TRUE;
						break;
					}

					/*****************************************************/
					/* Data was received                                 */
					/*****************************************************/
					len = rc;
					printf("  %d bytes received\n", len);

					/*****************************************************/
					/* Echo the data back to the client                  */
					/*****************************************************/
					rc = send(fds[i].fd, buffer, len, 0);
					if (rc < 0)
					{
						perror("  send() failed");
						close_conn = TRUE;
						break;
					}

				} while (TRUE);

				/*******************************************************/
				/* If the close_conn flag was turned on, we need       */
				/* to clean up this active connection. This            */
				/* clean up process includes removing the              */
				/* descriptor.                                         */
				/*******************************************************/
				if (close_conn)
				{
					close(fds[i].fd);
					fds[i].fd = -1;
					compress_array = TRUE;
				}

			} /* End of existing connection is readable             */
		}	  /* End of loop through pollable descriptors              */

		/***********************************************************/
		/* If the compress_array flag was turned on, we need       */
		/* to squeeze together the array and decrement the number  */
		/* of file descriptors. We do not need to move back the    */
		/* events and revents fields because the events will always*/
		/* be POLLIN in this case, and revents is output.          */
		/***********************************************************/
		if (compress_array)
		{
			compress_array = FALSE;
			for (i = 0; i < nfds; i++)
			{
				if (fds[i].fd == -1)
				{
					for (j = i; j < nfds - 1; j++)
					{
						fds[j].fd = fds[j + 1].fd;
					}
					i--;
					nfds--;
				}
			}
		}

	} while (end_server == FALSE); /* End of serving running.    */

	/*************************************************************/
	/* Clean up all of the sockets that are open                 */
	/*************************************************************/
	for (i = 0; i < nfds; i++)
	{
		if (fds[i].fd >= 0)
			close(fds[i].fd);
	}

	return EXIT_SUCCESS;
}

int thread_send_to_collector()
{
	int server_fd = socket(AF_INET, SOCK_STREAM, 0), c = 2; // dopo c volte smette di provare a connettersi

	while (connect(server_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
	{
		if (errno == ENOENT || errno == ECONNREFUSED)
		{
			printf("Client connecting...\n");
			sleep(2);
		}
		else
		{
			perror("connect");
			return EXIT_FAILURE;
		}
		c--;
		if (c == 0)
		{
			printf("Time finished...\n");
			return EXIT_FAILURE;
		}
	}
	printf("Client connected.\n");
	return server_fd;
}