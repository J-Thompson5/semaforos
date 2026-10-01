#include "../include/conn_queue.h"
#include <stdlib.h>
#include <stdio.h>

int conn_queue_init(conn_queue_t *q, size_t capacity)
{
    if (q == NULL || capacity == 0) {
        return -1;
    }

    q->buffer = malloc(capacity * sizeof(connection_t));
    if (q->buffer == NULL) {
        perror("malloc conn_queue buffer");
        return -1;
    }

    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->shutdown = 0;

    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        free(q->buffer);
        return -1;
    }

    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        perror("pthread_cond_init not_empty");
        pthread_mutex_destroy(&q->mutex);
        free(q->buffer);
        return -1;
    }

    if (pthread_cond_init(&q->not_full, NULL) != 0) {
        perror("pthread_cond_init not_full");
        pthread_cond_destroy(&q->not_empty);
        pthread_mutex_destroy(&q->mutex);
        free(q->buffer);
        return -1;
    }

    return 0;
}

void conn_queue_destroy(conn_queue_t *q)
{
    if (q == NULL) return;

    pthread_mutex_lock(&q->mutex);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);

    free(q->buffer);
    q->buffer = NULL;
    q->capacity = 0;
    q->count = 0;
}

int conn_queue_push(conn_queue_t *q, connection_t conn)
{
    if (q == NULL) return -1;

    pthread_mutex_lock(&q->mutex);

    while (q->count == q->capacity && !q->shutdown) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    if (q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    q->buffer[q->tail] = conn;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);

    return 0;
}

int conn_queue_pop(conn_queue_t *q, connection_t *out_conn)
{
    if (q == NULL || out_conn == NULL) return -1;

    pthread_mutex_lock(&q->mutex);

    while (q->count == 0 && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    if (q->count == 0 && q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    *out_conn = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);

    return 0;
}

void conn_queue_shutdown(conn_queue_t *q)
{
    if (q == NULL) return;

    pthread_mutex_lock(&q->mutex);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

size_t conn_queue_size(conn_queue_t *q)
{
    if (q == NULL) return 0;

    pthread_mutex_lock(&q->mutex);
    size_t size = q->count;
    pthread_mutex_unlock(&q->mutex);

    return size;
}
