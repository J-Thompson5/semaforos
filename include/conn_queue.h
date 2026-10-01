#ifndef CONN_QUEUE_H
#define CONN_QUEUE_H

#include <pthread.h>
#include <stddef.h>

typedef struct {
    int file_descriptor;
    unsigned long connection_id;
} connection_t;

typedef struct {
    connection_t *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} conn_queue_t;

int conn_queue_init(conn_queue_t *q, size_t capacity);
void conn_queue_destroy(conn_queue_t *q);
int conn_queue_push(conn_queue_t *q, connection_t conn);
int conn_queue_pop(conn_queue_t *q, connection_t *out_conn);
void conn_queue_shutdown(conn_queue_t *q);
size_t conn_queue_size(conn_queue_t *q);

#endif // CONN_QUEUE_H
