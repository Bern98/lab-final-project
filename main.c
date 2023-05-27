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
#include "unboundedqueue.h"
#include "errmsg.h"
#include "sfiles.h"

#define STOP "STOP"
#define N 5000
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
	char filename[4096];
} WorkerResults;

struct sockaddr_in sa;

static void read_and_calculate(char *filename, WorkerResults *resultsStruct);
static void recursive_unfold_and_push(char *, Queue *);
static void *main_thread_function(void *);
static void *worker_thread_function(void *);
static int collector_recieve_from_thread();
static int thread_send_to_collector();
// static char buf[N];
static int n_of_workers = 0;

int main(int argc, char **argv)
{
	sa.sin_addr.s_addr = inet_addr(SOCKET_IP);
	sa.sin_port = htons(42000);
	sa.sin_family = AF_INET;
	n_of_workers = atoi(argv[1]);

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
	// workerResults.filename = malloc(sizeof(char) * 150);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
		printf("socket creation failed...\n");
		exit(1);
	}
	else
		printf("Socket successfully created..\n");

	// connect the client socket to server socket
	if (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
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
	}

	while (1)
	{
		char *poppedDatum = (char *)pop(q);
		if (poppedDatum == NULL || strcmp(poppedDatum, STOP) == 0)
		{
			workerResults.n = -1;
			write(sockfd, &workerResults, sizeof(workerResults));
			break;
		}
		else
		{
			read_and_calculate(poppedDatum, &workerResults);
			if (write(sockfd, &workerResults, sizeof(workerResults)) == -1)
			{
				perror("thread_send_to_collector");
				exit(EXIT_FAILURE);
			}
		}

		// printf("%d\t%.2f\t%.2f\t%s\n", workerResults.n, workerResults.avg, workerResults.std, workerResults.filename);
		//  printf("[%ld]: %s\n", (long)pthread_self(), poppedDatum);

		free(poppedDatum);
	}

	// close the socket
	close(sockfd);
	pthread_exit(NULL);
}

//--------------------------Socket--------------------------------

int collector_recieve_from_thread()
{
	int sockfd, connfd = -1;

	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
		printf("socket creation failed...\n");
		exit(0);
	}
	else
		printf("Socket successfully created..\n");

	// Binding newly created socket to given IP and verification
	if ((bind(sockfd, (struct sockaddr *)&sa, sizeof(sa))) != 0)
	{
		printf("socket bind failed...\n");
		exit(0);
	}
	else
		printf("Socket successfully binded..\n");

	// Now server is ready to listen and verification
	if ((listen(sockfd, SOMAXCONN)) != 0)
	{
		printf("Listen failed...\n");
		exit(0);
	}
	else
		printf("Server listening..\n");

	// Accept the data packet from client and verification
	while (1)
	{
		if ((connfd = accept(sockfd, NULL, NULL)) < 0)
		{
			continue;
		}
		printf("Server connection accepted\n");
		break;
	}

	// Function for chatting between client and server
	int remaining_workers_active = n_of_workers;
	for (;;)
	{
		WorkerResults data;
		int chars_read = 0;
		fflush(stdout);

		// read the message from client and copy it in buffer
		chars_read = read(connfd, &data, sizeof(WorkerResults));
		if (chars_read < 0)
			return EXIT_FAILURE;

		// if msg contains "STOP"
		if (data.n < 0)
		{
			remaining_workers_active--;
			if (remaining_workers_active == 0)
				break;
		}

		printf("%d\t%lf\t%lf\t%s\n", data.n, data.avg, data.std, data.filename);
	}

	// After chatting close the socket
	close(sockfd);
	printf("Server Exit...\n");
	return EXIT_SUCCESS;
}