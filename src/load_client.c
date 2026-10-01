//
// Modified load_client with precise metrics and flexible load distribution
//

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    struct sockaddr_in server;
    unsigned long requests;
    unsigned long completed;   /* written only by this thread */
} worker_args_t;

static int send_one_request(const struct sockaddr_in *server)
{
    int file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (file_descriptor < 0)
        return -1;

    if (connect(file_descriptor, (const struct sockaddr *)server, sizeof *server) < 0) {
        close(file_descriptor);
        return -1;
    }

    static const char request[] = "GET / HTTP/1.1\r\nHost: bench\r\n\r\n";
    if (write(file_descriptor, request, sizeof request - 1) < 0) {
        close(file_descriptor);
        return -1;
    }

    char buffer[1024];
    while (read(file_descriptor, buffer, sizeof buffer) > 0)
        ;   /* read until the server closes */

    close(file_descriptor);
    return 0;
}

static void *worker(void *arg)
{
    worker_args_t *args = arg;

    for (unsigned long i = 0; i < args->requests; ++i) {
        if (send_one_request(&args->server) == 0)
            ++args->completed;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <host> <port> <threads> <requests-per-thread|total-requests> [--total]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof server);
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)atoi(argv[2]));

    if (inet_pton(AF_INET, argv[1], &server.sin_addr) != 1) {
        fprintf(stderr, "invalid host '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    long thread_count = atol(argv[3]);
    long count_arg = atol(argv[4]);
    if (thread_count <= 0 || count_arg <= 0) {
        fprintf(stderr, "threads and requests must be positive\n");
        return EXIT_FAILURE;
    }

    int is_total_mode = 0;
    if (argc >= 6 && strcmp(argv[5], "--total") == 0) {
        is_total_mode = 1;
    } else if (getenv("TOTAL_REQUESTS_MODE") != NULL) {
        is_total_mode = 1;
    }

    pthread_t *tids = calloc((size_t)thread_count, sizeof *tids);
    worker_args_t *args = calloc((size_t)thread_count, sizeof *args);

    if (tids == NULL || args == NULL) {
        fprintf(stderr, "out of memory\n");
        free(tids);
        free(args);
        return EXIT_FAILURE;
    }

    long base_requests = count_arg;
    long remainder = 0;
    if (is_total_mode) {
        base_requests = count_arg / thread_count;
        remainder = count_arg % thread_count;
    }

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    long started = 0;
    for (long i = 0; i < thread_count; ++i) {
        args[i].server = server;
        args[i].requests = (unsigned long)(base_requests + (is_total_mode && (i < remainder) ? 1 : 0));
        args[i].completed = 0;

        int rc = pthread_create(&tids[i], NULL, worker, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            break;
        }
        ++started;
    }

    unsigned long total = 0;
    for (long i = 0; i < started; ++i) {
        int rc = pthread_join(tids[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(rc));
        } else {
            total += args[i].completed;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double elapsed_sec = (double)(end_time.tv_sec - start_time.tv_sec) +
                         (double)(end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    double throughput = (elapsed_sec > 0.0) ? ((double)total / elapsed_sec) : 0.0;
    double avg_latency_ms = (total > 0) ? (elapsed_sec * 1000.0 / (double)total) : 0.0;

    printf("requests completed: %lu\n", total);
    printf("elapsed time: %.4f s\n", elapsed_sec);
    printf("throughput: %.2f req/s\n", throughput);
    printf("avg latency: %.4f ms\n", avg_latency_ms);
    printf("[METRICS] completed=%lu elapsed_s=%.6f throughput=%.2f latency_ms=%.4f\n",
           total, elapsed_sec, throughput, avg_latency_ms);

    free(tids);
    free(args);
    return EXIT_SUCCESS;
}
