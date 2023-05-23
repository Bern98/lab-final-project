#include "unboundedqueue.h"

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