#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "functions.h"

void init_queue(Queue *q)
{
	pthread_mutex_init(&(q->mtx), NULL);
	pthread_cond_init(&(q->cond), NULL);
	q->head = NULL;
	q->tail = NULL;
}

void queue_destroy(Queue *q)
{
	pthread_mutex_destroy(&q->mtx);
	pthread_cond_destroy(&q->cond);
	while (q->head)
	{
		Node *temp = q->head;
		q->head = q->head->next;
		free(temp);
	}
	q->tail = NULL;
}

void push(void *data, Queue *q)
{
	Node *newNode = (Node *)malloc(sizeof(Node));
	newNode->data = data;
	newNode->next = NULL;

	pthread_mutex_lock(&(q->mtx));
	if (q->tail == NULL)
	{
		q->head = newNode;
	}
	else
	{
		q->tail->next = newNode;
	}
	q->tail = newNode;

	pthread_cond_signal(&(q->cond));
	pthread_mutex_unlock(&(q->mtx));
	return;
}

void *pop(Queue *q)
{
	pthread_mutex_lock(&(q->mtx));
	while (q->head == NULL)
		pthread_cond_wait(&(q->cond), &(q->mtx));

	Node *n = q->head;
	q->head = q->head->next;

	if (q->head == NULL) // caso in cui ci sia un unico elemento aggiorno anche la coda
		q->tail = NULL;

	pthread_cond_signal(&(q->cond));
	pthread_mutex_unlock(&(q->mtx));

	void *data = n->data;
	free(n);

	return data;
}

void recursiveUnfoldAndPush(char *dirPath, Queue *q)
{
	DIR *dir = opendir(dirPath);
	if (dir == NULL)
	{
		perror("Error opening directory.\n");
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
		{
			continue;
		}

		char entryPath[1024];
		snprintf(entryPath, sizeof(entryPath), "%s/%s", dirPath, entry->d_name);

		struct stat entryStat;
		if (lstat(entryPath, &entryStat) == -1)
		{
			perror("Error getting file status.\n");
			continue;
		}

		if (S_ISDIR(entryStat.st_mode))
		{
			recursiveUnfoldAndPush(entryPath, q);
		}
		else if (S_ISREG(entryStat.st_mode))
		{
			push((void *)entryPath, q);
		}
	}

	if (closedir(dir) == -1)
	{
		perror("Error closing directory.\n");
		return;
	}
}

void readAndCalc(char *filename, WorkerResults *resultsStruct)
{
	int n = 0;
	double sum = 0, sumSq = 0, avg, variance, std;
	char *data, *token;
	FILE *fp;
	//printf("f:%s\n", filename);
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

void *mainThreadFunction(void *args)
{
	ThreadArgs *Args = (ThreadArgs *)args;
	char *dirname = Args->dirname;
	Queue *q = Args->queue;
	int n_of_threads = Args->n_of_threads;
	recursiveUnfoldAndPush(dirname, q);
	for (int i = 0; i < n_of_threads; i++)
		push((void *)STOP, q);
	pthread_exit(NULL);
}

void *workerThreadPrint(void *args)
{
	ThreadArgs *Args = (ThreadArgs *)args;
	Queue *q = Args->queue;
	char* poppedDatum = malloc(sizeof(char)*200);
	WorkerResults workerResults;
	
	while (1)
	{
		poppedDatum = (char*)pop(q);
		if (poppedDatum == NULL || strcmp(poppedDatum, STOP) == 0) break;
		else printf("[%ld]: %s\n", (long)pthread_self() ,poppedDatum);

		//printf("%d\t%.2f\t%.2f\t%s\n", workerResults.n, workerResults.avg, workerResults.std, workerResults.filename);
	}

	pthread_exit(NULL);
}

//--------------------------Socket--------------------------------