
#define UNIX_PATH_MAX 108
#define SOCKNAME "./mysock"
#define N 100
#define ERROR_CHECK(msg, func, ret) \
	if (func == ret)            \
	{                          \
		perror(msg);           \
		exit(EXIT_FAILURE);    \
	}
#define STOP "STOP"


typedef struct n{
    void* data;
    struct n* next;
} Node;

typedef struct{
    Node* head;
    Node* tail;
    pthread_mutex_t mtx;
	pthread_cond_t cond;
} Queue;

typedef struct {
	Queue *queue;
	char* dirname;
	int n_of_threads;
} ThreadArgs;

typedef struct
{
	int n;
	double avg;
	double std;
	char* filename;
} WorkerResults;

void init_queue(Queue* q);

void queue_destroy(Queue* q);

void push(void* data, Queue* q);

void* pop(Queue* q);

void readAndCalc(char *filename, WorkerResults *resultsStruct);

void recursiveUnfoldAndPush(char*, Queue*);

void* mainThreadFunction(void*);

void* workerThreadPrint(void*);