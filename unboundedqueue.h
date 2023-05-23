#ifndef UNBOUNDEDQUEUE_H
#define UNBOUNDEDQUEUE_H

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct n
{
    void *data;
    struct n *next;
} Node;

typedef struct
{
    Node *head;
    Node *tail;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
} Queue;

void init_queue(Queue *q);
void queue_destroy(Queue *q);
void push(void *data, Queue *q);
void *pop(Queue *q);
#endif