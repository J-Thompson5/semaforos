#define _POSIX_C_SOURCE 200809L

#include "../include/conn_queue.h"
#include "../include/net_util.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define DEFAULT_WORKER_THREADS 4
#define DEFAULT_QUEUE_CAPACITY 256
#define LISTEN_BACKLOG 128
#define DRAIN_SECONDS 1

static volatile sig_atomic_t g_running = 1;
static unsigned long g_requests_served = 0;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static conn_queue_t g_queue;
static int g_listen_fd = -1;
static int g_quiet = 0;

static void on_sigint(int signum)
{
    (void)signum;
    g_running = 0;
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
    }
}

static int install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction SIGINT");
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        perror("sigaction SIGPIPE");
        return -1;
    }

    return 0;
}

static void *worker_thread(void *arg)
{
    long thread_id = (long)arg;
    connection_t conn;

    while (conn_queue_pop(&g_queue, &conn) == 0) {
        if (!g_quiet) {
            printf("[Worker %ld] Handling connection %lu\n", thread_id, conn.connection_id);
            fflush(stdout);
        }

        nu_drain_request(conn.file_descriptor);
        (void)nu_send_response(conn.file_descriptor, conn.connection_id);

        pthread_mutex_lock(&g_stats_mutex);
        g_requests_served++;
        pthread_mutex_unlock(&g_stats_mutex);

        if (close(conn.file_descriptor) < 0) {
            perror("close(file_descriptor)");
        }
    }

    return NULL;
}

static unsigned short parse_port(const char *arg)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0' || value <= 0 || value > 65535) {
        fprintf(stderr, "Invalid port '%s', defaulting to %d\n", arg, DEFAULT_PORT);
        return DEFAULT_PORT;
    }
    return (unsigned short)value;
}

int main(int argc, char **argv)
{
    if (install_signal_handlers() < 0) {
        return EXIT_FAILURE;
    }

    unsigned short port = DEFAULT_PORT;
    long worker_count = DEFAULT_WORKER_THREADS;
    size_t queue_capacity = DEFAULT_QUEUE_CAPACITY;

    if (argc >= 2) {
        port = parse_port(argv[1]);
    }
    if (argc >= 3) {
        long val = atol(argv[2]);
        if (val > 0) {
            worker_count = val;
        }
    }
    if (argc >= 4) {
        long val = atol(argv[3]);
        if (val > 0) {
            queue_capacity = (size_t)val;
        }
    }
    if (getenv("SERVER_QUIET") != NULL) {
        g_quiet = 1;
    }

    if (conn_queue_init(&g_queue, queue_capacity) != 0) {
        fprintf(stderr, "Failed to initialize connection queue\n");
        return EXIT_FAILURE;
    }

    pthread_t *workers = calloc((size_t)worker_count, sizeof(pthread_t));
    if (workers == NULL) {
        fprintf(stderr, "Out of memory allocating worker threads\n");
        conn_queue_destroy(&g_queue);
        return EXIT_FAILURE;
    }

    for (long i = 0; i < worker_count; ++i) {
        int rc = pthread_create(&workers[i], NULL, worker_thread, (void *)i);
        if (rc != 0) {
            fprintf(stderr, "pthread_create worker %ld failed: %s\n", i, strerror(rc));
            worker_count = i;
            break;
        }
    }

    g_listen_fd = nu_listen(port, LISTEN_BACKLOG);
    if (g_listen_fd < 0) {
        conn_queue_shutdown(&g_queue);
        for (long i = 0; i < worker_count; ++i) {
            pthread_join(workers[i], NULL);
        }
        free(workers);
        conn_queue_destroy(&g_queue);
        return EXIT_FAILURE;
    }

    printf("Producer-Consumer server running on port %u with %ld worker threads (queue capacity %zu) — Ctrl-C to stop\n",
           port, worker_count, queue_capacity);
    fflush(stdout);

    unsigned long accepted = 0;

    while (g_running) {
        int client_fd = accept(g_listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR || !g_running) {
                break;
            }
            perror("accept");
            break;
        }

        connection_t conn;
        conn.file_descriptor = client_fd;
        conn.connection_id = ++accepted;

        if (conn_queue_push(&g_queue, conn) != 0) {
            if (!g_running) {
                close(client_fd);
                --accepted;
                break;
            }
            fprintf(stderr, "Failed to enqueue connection %lu\n", conn.connection_id);
            close(client_fd);
            --accepted;
        }
    }

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    conn_queue_shutdown(&g_queue);

    for (long i = 0; i < worker_count; ++i) {
        pthread_join(workers[i], NULL);
    }

    free(workers);
    conn_queue_destroy(&g_queue);
    pthread_mutex_destroy(&g_stats_mutex);

    printf("\n--- Server Statistics ---\n");
    printf("accepted: %lu\n", accepted);
    printf("served:   %lu\n", g_requests_served);
    printf("lost:     %ld\n", (long)accepted - (long)g_requests_served);

    return EXIT_SUCCESS;
}
