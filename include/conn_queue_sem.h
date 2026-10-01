#ifndef CONN_QUEUE_SEM_H
#define CONN_QUEUE_SEM_H

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

typedef struct {
    int file_descriptor;
    unsigned long connection_id;
} connection_sem_t;

typedef struct {
    connection_sem_t *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    int shutdown;

    pthread_mutex_t mutex;

    sem_t *sem_spaces;
    sem_t *sem_items;

#ifdef __APPLE__
    char name_spaces[64];
    char name_items[64];
#else
    sem_t sem_spaces_storage;
    sem_t sem_items_storage;
#endif
} conn_queue_sem_t;

int conn_queue_sem_init(conn_queue_sem_t *q, size_t capacity);
void conn_queue_sem_destroy(conn_queue_sem_t *q);
int conn_queue_sem_push(conn_queue_sem_t *q, connection_sem_t conn);
int conn_queue_sem_pop(conn_queue_sem_t *q, connection_sem_t *out_conn);
void conn_queue_sem_shutdown(conn_queue_sem_t *q);
size_t conn_queue_sem_size(conn_queue_sem_t *q);

#endif // CONN_QUEUE_SEM_H
