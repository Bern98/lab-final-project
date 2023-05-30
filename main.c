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
#define SOCKET_IP "127.0.0.1"
#define MAX_THREADS 4096

typedef struct
{
	Queue *queue;
	char *dirname;
	int n_of_threads;
	int sockfd;
} ThreadArgs;

typedef struct
{
	int sockfd;
} CollectorThreadArgs;

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
static void *collector_thread_function(void *);

static int n_of_workers = 0, dat_files_found = 0, dat_files_printed = 0;

int main(int argc, char **argv)
{
	sa.sin_addr.s_addr = inet_addr(SOCKET_IP);
	die_if_error("inet_addr", sa.sin_addr.s_addr);
	sa.sin_port = htons(42000);
	die_if_error("htons", sa.sin_port);
	sa.sin_family = AF_INET;
	n_of_workers = atoi(argv[1]);
	die_if_error("atoi", n_of_workers);

	if (argc < 3)
	{
		fflush(stdout);
		printf("try ./main <number of threads> <dirname>\n");
		return EXIT_FAILURE;
	}

	if (fork() != 0) //* Padre - Server
	{
		if (collector_recieve_from_thread() == -1)
		{
			perror("collector_recieve_from_thread");
			exit(EXIT_FAILURE);
		}

		printf("\nprinted: %d\n", dat_files_printed);

		exit(EXIT_SUCCESS);
	}
	else
	{ //* Figlio - Client
		int n_of_threads = atoi(argv[1]);
		char *dirname = argv[2];

		die_if_error("chdr", chdir(dirname)); // Cambiare directory per far funzionare Recursive Unfold

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
			ThreadArgs.sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if (pthread_create(&threads_array[i], NULL, &worker_thread_function, &ThreadArgs) != 0)
			{
				perror("pthread_create");
				exit(EXIT_FAILURE);
			}
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
		}
		queue_destroy(ThreadArgs.queue);
		free(ThreadArgs.queue);
		free(ThreadArgs.dirname);

		printf("\nfound: %d\n", dat_files_found);
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
			{
				push(strdup(entryPath), q);
				dat_files_found++;
			}
		}
	}
	die_if_error("closedir", closedir(dir));
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

	// Calcola somma e quadrato
	token = strtok(data, " \t\n");
	while (token != NULL)
	{
		double num = atof(token);
		sum += num;
		sumSq += num * num;
		n++;
		token = strtok(NULL, " \t\n");
	}

	// Calcola media e varianza
	avg = sum / n;
	variance = (sumSq / n) - (avg * avg);

	// Calcola deviazione std
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
	memset(&workerResults, 0, sizeof(WorkerResults));

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
		printf("socket creation failed...\n");
		exit(1);
	}

	// Collega il client socket al server socket
	while (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
	{
		if (errno == ENOENT || errno == ECONNREFUSED)
			sleep(2);
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
			strcpy(workerResults.filename, STOP);
			if (write(sockfd, &workerResults, sizeof(workerResults)) == -1)
			{
				perror("thread_send_to_collector");
				exit(EXIT_FAILURE);
			}
			break;
		}
		else
		{
			memset(&workerResults, 0, sizeof(WorkerResults));
			read_and_calculate(poppedDatum, &workerResults);
			if (write(sockfd, &workerResults, sizeof(workerResults)) == -1)
			{
				perror("thread_send_to_collector");
				exit(EXIT_FAILURE);
			}
		}
		free(poppedDatum);
	}

	// Chiudere il socket
	close(sockfd);
	pthread_exit(NULL);
}

//--------------------------Socket--------------------------------

int collector_recieve_from_thread()
{
	int sockfd, connfd[MAX_THREADS];
	pthread_t collector_threads[MAX_THREADS];
	int active_collector_threads = 0;
	CollectorThreadArgs thread_args;

	memset(connfd, -1, MAX_THREADS * sizeof(int));
	memset(collector_threads, 0, MAX_THREADS * sizeof(pthread_t));

	// Crea socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	die_if_error("collector socket create", sockfd);
	// Bind del socket con l'indirizzo IP specificato dentro la struttura "sa"
	die_if_error("collector bind", bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)));

	// Mettere il server in ascolto
	die_if_error("collector listen", listen(sockfd, SOMAXCONN));

	thread_args.sockfd = sockfd;
	// Accettare dati dai client
	while (active_collector_threads < n_of_workers)
	{
		// Crea 1 thread per gestire 1 worker che invia dati al server
		die_if_error("collector pthread_create", pthread_create(&collector_threads[active_collector_threads], NULL, &collector_thread_function, &thread_args));
		active_collector_threads++;
	}

	for (int i = 0; i < active_collector_threads; i++)
	{
		die_if_error("collector pthread_join", pthread_join(collector_threads[i], NULL));
		close(connfd[i]);
	}
	// Chiudere e uscire
	close(sockfd);
	return EXIT_SUCCESS;
}

void *collector_thread_function(void *args)
{
	CollectorThreadArgs *Args = (CollectorThreadArgs *)args;
	WorkerResults data;
	int sockfd = Args->sockfd, connfd;

	connfd = accept(sockfd, NULL, NULL);
	die_if_error("collector thread accept", connfd);

	for (;;)
	{
		memset(&data, 0, sizeof(WorkerResults));
		int read_bytes = 0;

		// Leggi messaggio dal client e mettere nel buffer "data"
		if ((read_bytes = read(connfd, &data, sizeof(WorkerResults))) < 0)
		{
			perror("read");
			break;
		}
		// Se il messaggio contiene la condizione d'uscita, filename = "STOP", oppure se si ha EOF, esci
		if (strcmp(data.filename, STOP) == 0 || read_bytes == 0)
			break;
		else
		{
			printf("%d\t%lf\t%lf\t%s\n", data.n, data.avg, data.std, data.filename);
			fflush(stdout);
			dat_files_printed++;
		}
	}
	pthread_exit(NULL);
}