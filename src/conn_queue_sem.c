#define _POSIX_C_SOURCE 200809L

#include "../include/conn_queue_sem.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int init_semaphores(conn_queue_sem_t *q, size_t capacity)
{
#ifdef __APPLE__
    snprintf(q->name_spaces, sizeof(q->name_spaces), "/cq_sp_%d_%p", getpid(), (void *)q);
    snprintf(q->name_items, sizeof(q->name_items), "/cq_it_%d_%p", getpid(), (void *)q);

    sem_unlink(q->name_spaces);
    sem_unlink(q->name_items);

    q->sem_spaces = sem_open(q->name_spaces, O_CREAT | O_EXCL, 0644, (unsigned int)capacity);
    if (q->sem_spaces == SEM_FAILED) {
        perror("sem_open sem_spaces");
        return -1;
    }

    q->sem_items = sem_open(q->name_items, O_CREAT | O_EXCL, 0644, 0);
    if (q->sem_items == SEM_FAILED) {
        perror("sem_open sem_items");
        sem_close(q->sem_spaces);
        sem_unlink(q->name_spaces);
        return -1;
    }
#else
    if (sem_init(&q->sem_spaces_storage, 0, (unsigned int)capacity) != 0) {
        perror("sem_init sem_spaces");
        return -1;
    }
    q->sem_spaces = &q->sem_spaces_storage;

    if (sem_init(&q->sem_items_storage, 0, 0) != 0) {
        perror("sem_init sem_items");
        sem_destroy(&q->sem_spaces_storage);
        return -1;
    }
    q->sem_items = &q->sem_items_storage;
#endif

    return 0;
}

static void cleanup_semaphores(conn_queue_sem_t *q)
{
#ifdef __APPLE__
    if (q->sem_spaces && q->sem_spaces != SEM_FAILED) {
        sem_close(q->sem_spaces);
        sem_unlink(q->name_spaces);
        q->sem_spaces = NULL;
    }
    if (q->sem_items && q->sem_items != SEM_FAILED) {
        sem_close(q->sem_items);
        sem_unlink(q->name_items);
        q->sem_items = NULL;
    }
#else
    sem_destroy(&q->sem_spaces_storage);
    sem_destroy(&q->sem_items_storage);
#endif
}

int conn_queue_sem_init(conn_queue_sem_t *q, size_t capacity)
{
    if (q == NULL || capacity == 0) {
        return -1;
    }

    q->buffer = malloc(capacity * sizeof(connection_sem_t));
    if (q->buffer == NULL) {
        perror("malloc conn_queue_sem buffer");
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

    if (init_semaphores(q, capacity) != 0) {
        pthread_mutex_destroy(&q->mutex);
        free(q->buffer);
        return -1;
    }

    return 0;
}

void conn_queue_sem_destroy(conn_queue_sem_t *q)
{
    if (q == NULL) return;

    conn_queue_sem_shutdown(q);

    pthread_mutex_destroy(&q->mutex);
    cleanup_semaphores(q);

    free(q->buffer);
    q->buffer = NULL;
    q->capacity = 0;
    q->count = 0;
}

int conn_queue_sem_push(conn_queue_sem_t *q, connection_sem_t conn)
{
    if (q == NULL) return -1;

    while (sem_wait(q->sem_spaces) != 0) {
        if (errno == EINTR) {
            pthread_mutex_lock(&q->mutex);
            int is_shutdown = q->shutdown;
            pthread_mutex_unlock(&q->mutex);
            if (is_shutdown) return -1;
            continue;
        }
        return -1;
    }

    pthread_mutex_lock(&q->mutex);

    if (q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        sem_post(q->sem_spaces);
        return -1;
    }

    q->buffer[q->tail] = conn;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_mutex_unlock(&q->mutex);

    sem_post(q->sem_items);

    return 0;
}

int conn_queue_sem_pop(conn_queue_sem_t *q, connection_sem_t *out_conn)
{
    if (q == NULL || out_conn == NULL) return -1;

    while (sem_wait(q->sem_items) != 0) {
        if (errno == EINTR) {
            pthread_mutex_lock(&q->mutex);
            int is_empty_and_shutdown = (q->shutdown && q->count == 0);
            pthread_mutex_unlock(&q->mutex);
            if (is_empty_and_shutdown) return -1;
            continue;
        }
        return -1;
    }

    pthread_mutex_lock(&q->mutex);

    if (q->shutdown && q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        sem_post(q->sem_items);
        return -1;
    }

    *out_conn = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_mutex_unlock(&q->mutex);

    sem_post(q->sem_spaces);

    return 0;
}

void conn_queue_sem_shutdown(conn_queue_sem_t *q)
{
    if (q == NULL) return;

    pthread_mutex_lock(&q->mutex);
    if (q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        return;
    }
    q->shutdown = 1;
    pthread_mutex_unlock(&q->mutex);

    for (size_t i = 0; i < q->capacity + 128; ++i) {
        sem_post(q->sem_items);
        sem_post(q->sem_spaces);
    }
}

size_t conn_queue_sem_size(conn_queue_sem_t *q)
{
    if (q == NULL) return 0;

    pthread_mutex_lock(&q->mutex);
    size_t size = q->count;
    pthread_mutex_unlock(&q->mutex);

    return size;
}
