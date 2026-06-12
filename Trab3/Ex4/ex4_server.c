#define _POSIX_C_SOURCE 200809L

#include "../Ex2/threadpool.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define UNIX_SOCKET_PATH "/tmp/SocketSO_Ex4"
#define ST_OK 0U
#define ST_INVALID 1U
#define ST_ERR 2U

typedef struct {
    uint16_t min;
    uint16_t max;
    uint64_t sum;
} Stats;

typedef struct {
    const uint16_t *values;
    size_t start;
    size_t end;
    Stats out;
} WorkerTask;

typedef struct {
    pthread_mutex_t mutex;
    unsigned long long unix_connections;
    unsigned long long internet_connections;
    unsigned long long total_vector_elems;
    unsigned long long processed_vectors;
} ServerStats;

typedef struct {
    int conn_fd;
    int is_unix;
} ClientJob;

static threadpool_t g_pool;
static ServerStats g_stats = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static ssize_t read_full(int fd, void *buf, size_t n) {
    unsigned char *p = buf;
    size_t got = 0;

    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static int write_full(int fd, const void *buf, size_t n) {
    const unsigned char *p = buf;
    size_t sent = 0;

    while (sent < n) {
        ssize_t w = write(fd, p + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)w;
    }
    return 0;
}

static uint64_t htonll(uint64_t v) {
    uint32_t hi = htonl((uint32_t)(v >> 32));
    uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFFU));
    return ((uint64_t)lo << 32) | hi;
}

static int write_u8(int fd, uint8_t v) {
    return write_full(fd, &v, 1U);
}

static int read_u32_be(int fd, uint32_t *out) {
    uint32_t be = 0;
    ssize_t r = read_full(fd, &be, sizeof(be));
    if (r <= 0) {
        return -1;
    }
    *out = ntohl(be);
    return 0;
}

static int send_status_msg(int fd, uint8_t st, const char *msg) {
    uint32_t be = htonl((uint32_t)strlen(msg));
    if (write_u8(fd, st) != 0) {
        return -1;
    }
    if (write_full(fd, &be, sizeof(be)) != 0) {
        return -1;
    }
    if (write_full(fd, msg, strlen(msg)) != 0) {
        return -1;
    }
    return 0;
}

static void stats_record_connection(int is_unix) {
    pthread_mutex_lock(&g_stats.mutex);
    if (is_unix) {
        g_stats.unix_connections++;
    } else {
        g_stats.internet_connections++;
    }
    pthread_mutex_unlock(&g_stats.mutex);
}

static void stats_record_vector(size_t count) {
    pthread_mutex_lock(&g_stats.mutex);
    g_stats.total_vector_elems += (unsigned long long)count;
    g_stats.processed_vectors++;
    pthread_mutex_unlock(&g_stats.mutex);
}

static void *worker_fn(void *arg) {
    WorkerTask *t = (WorkerTask *)arg;
    t->out.min = UINT16_MAX;
    t->out.max = 0;
    t->out.sum = 0;
    for (size_t i = t->start; i < t->end; ++i) {
        uint16_t v = t->values[i];
        if (v < t->out.min) {
            t->out.min = v;
        }
        if (v > t->out.max) {
            t->out.max = v;
        }
        t->out.sum += v;
    }
    return NULL;
}

static int compute_stats_parallel(const uint16_t *values, size_t n, Stats *out) {
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    size_t workers = (cpus > 0) ? (size_t)cpus : 4U;
    if (workers > n) {
        workers = n;
    }
    if (workers == 0) {
        return -1;
    }

    pthread_t *tids = calloc(workers, sizeof(*tids));
    WorkerTask *tasks = calloc(workers, sizeof(*tasks));
    if (tids == NULL || tasks == NULL) {
        free(tids);
        free(tasks);
        return -1;
    }

    size_t base = n / workers;
    size_t rem = n % workers;
    size_t cursor = 0;
    for (size_t i = 0; i < workers; ++i) {
        size_t len = base + (i < rem ? 1U : 0U);
        tasks[i].values = values;
        tasks[i].start = cursor;
        tasks[i].end = cursor + len;
        cursor += len;
        if (pthread_create(&tids[i], NULL, worker_fn, &tasks[i]) != 0) {
            free(tids);
            free(tasks);
            return -1;
        }
    }

    out->min = UINT16_MAX;
    out->max = 0;
    out->sum = 0;
    for (size_t i = 0; i < workers; ++i) {
        if (pthread_join(tids[i], NULL) != 0) {
            free(tids);
            free(tasks);
            return -1;
        }
        if (tasks[i].out.min < out->min) {
            out->min = tasks[i].out.min;
        }
        if (tasks[i].out.max > out->max) {
            out->max = tasks[i].out.max;
        }
        out->sum += tasks[i].out.sum;
    }

    free(tids);
    free(tasks);
    return 0;
}

static void handle_client(void *arg) {
    ClientJob *job = (ClientJob *)arg;
    int fd = job->conn_fd;
    free(job);

    uint16_t *values = NULL;
    size_t count = 0;
    size_t cap = 0;
    int parse_error = 0;
    char errbuf[128] = "pedido invalido";

    while (1) {
        uint32_t len = 0;
        if (read_u32_be(fd, &len) != 0) {
            parse_error = 1;
            snprintf(errbuf, sizeof(errbuf), "pedido truncado");
            break;
        }
        if (len == 0U) {
            break;
        }
        if ((len % sizeof(uint16_t)) != 0U) {
            parse_error = 1;
            snprintf(errbuf, sizeof(errbuf), "bloco com dimensao invalida");
            break;
        }
        size_t elems = (size_t)(len / sizeof(uint16_t));
        if (count > SIZE_MAX - elems) {
            parse_error = 1;
            snprintf(errbuf, sizeof(errbuf), "vetor demasiado grande");
            break;
        }
        if (count + elems > cap) {
            size_t new_cap = (cap == 0U) ? 1024U : cap;
            while (new_cap < count + elems) {
                if (new_cap > SIZE_MAX / 2U) {
                    parse_error = 1;
                    snprintf(errbuf, sizeof(errbuf), "vetor demasiado grande");
                    break;
                }
                new_cap *= 2U;
            }
            if (parse_error) {
                break;
            }
            uint16_t *tmp = realloc(values, new_cap * sizeof(*values));
            if (tmp == NULL) {
                (void)send_status_msg(fd, ST_ERR, "falha de memoria");
                free(values);
                close(fd);
                return;
            }
            values = tmp;
            cap = new_cap;
        }
        for (size_t i = 0; i < elems; ++i) {
            uint16_t be = 0;
            ssize_t rr = read_full(fd, &be, sizeof(be));
            if (rr <= 0) {
                parse_error = 1;
                snprintf(errbuf, sizeof(errbuf), "pedido truncado");
                break;
            }
            values[count++] = ntohs(be);
        }
        if (parse_error) {
            break;
        }
    }

    if (!parse_error && count == 0U) {
        parse_error = 1;
        snprintf(errbuf, sizeof(errbuf), "vetor vazio");
    }

    if (parse_error) {
        (void)send_status_msg(fd, ST_INVALID, errbuf);
        free(values);
        close(fd);
        return;
    }

    Stats s;
    if (compute_stats_parallel(values, count, &s) != 0) {
        (void)send_status_msg(fd, ST_ERR, "erro no processamento paralelo");
        free(values);
        close(fd);
        return;
    }

    stats_record_vector(count);

    if (write_u8(fd, ST_OK) != 0) {
        free(values);
        close(fd);
        return;
    }

    uint16_t min_be = htons(s.min);
    uint16_t max_be = htons(s.max);
    uint64_t sum_be = htonll(s.sum);
    if (write_full(fd, &min_be, sizeof(min_be)) != 0 ||
        write_full(fd, &max_be, sizeof(max_be)) != 0 ||
        write_full(fd, &sum_be, sizeof(sum_be)) != 0) {
        free(values);
        close(fd);
        return;
    }

    free(values);
    close(fd);
}

static void submit_client(int conn_fd, int is_unix) {
    ClientJob *job = malloc(sizeof(*job));
    if (job == NULL) {
        close(conn_fd);
        return;
    }

    job->conn_fd = conn_fd;
    job->is_unix = is_unix;
    stats_record_connection(is_unix);

    if (threadpool_submit(&g_pool, handle_client, job) != 0) {
        free(job);
        close(conn_fd);
    }
}

static int listen_unix_stream(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket AF_UNIX");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "socket path too long\n");
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1U);

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind AF_UNIX");
        close(fd);
        return -1;
    }
    if (listen(fd, 64) != 0) {
        perror("listen AF_UNIX");
        close(fd);
        return -1;
    }
    return fd;
}

static int listen_tcp_stream(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket AF_INET");
        return -1;
    }

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind AF_INET");
        close(fd);
        return -1;
    }
    if (listen(fd, 64) != 0) {
        perror("listen AF_INET");
        close(fd);
        return -1;
    }
    return fd;
}

static void *print_statistics(void *arg) {
    (void)arg;

    while (!g_stop) {
        sleep(1);

        pthread_mutex_lock(&g_stats.mutex);
        unsigned long long unix_conn = g_stats.unix_connections;
        unsigned long long inet_conn = g_stats.internet_connections;
        unsigned long long total_elems = g_stats.total_vector_elems;
        unsigned long long vectors = g_stats.processed_vectors;
        pthread_mutex_unlock(&g_stats.mutex);

        double avg = 0.0;
        if (vectors > 0ULL) {
            avg = (double)total_elems / (double)vectors;
        }

        printf("[estatisticas] unix=%llu internet=%llu dim_media=%.2f\n",
               unix_conn, inet_conn, avg);
        fflush(stdout);
    }

    return NULL;
}

static void serve_loop(int unix_fd, int tcp_fd) {
    struct pollfd pfds[2];

    pfds[0].fd = unix_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd = tcp_fd;
    pfds[1].events = POLLIN;

    while (!g_stop) {
        int pr = poll(pfds, 2, 500);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        if (pr == 0) {
            continue;
        }

        if ((pfds[0].revents & POLLIN) != 0) {
            int conn = accept(unix_fd, NULL, NULL);
            if (conn < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("accept unix");
                continue;
            }
            submit_client(conn, 1);
        }

        if ((pfds[1].revents & POLLIN) != 0) {
            int conn = accept(tcp_fd, NULL, NULL);
            if (conn < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("accept tcp");
                continue;
            }
            submit_client(conn, 0);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "uso: %s <porto> [num_workers]\n", argv[0]);
        return EXIT_FAILURE;
    }

    unsigned long port = strtoul(argv[1], NULL, 10);
    if (port == 0UL || port > 65535UL) {
        fprintf(stderr, "porto invalido\n");
        return EXIT_FAILURE;
    }

    size_t pool_workers = 0;
    if (argc == 3) {
        pool_workers = strtoull(argv[2], NULL, 10);
        if (pool_workers == 0U) {
            fprintf(stderr, "num_workers invalido\n");
            return EXIT_FAILURE;
        }
    } else {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        pool_workers = (cpus > 0) ? (size_t)cpus : 4U;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (threadpool_init(&g_pool, pool_workers) != 0) {
        perror("threadpool_init");
        return EXIT_FAILURE;
    }

    pthread_t stats_thread;
    if (pthread_create(&stats_thread, NULL, print_statistics, NULL) != 0) {
        perror("pthread_create printStatistics");
        threadpool_destroy(&g_pool);
        return EXIT_FAILURE;
    }

    int unix_fd = listen_unix_stream(UNIX_SOCKET_PATH);
    int tcp_fd = listen_tcp_stream((uint16_t)port);
    if (unix_fd < 0 || tcp_fd < 0) {
        g_stop = 1;
        pthread_join(stats_thread, NULL);
        threadpool_destroy(&g_pool);
        if (unix_fd >= 0) {
            close(unix_fd);
        }
        if (tcp_fd >= 0) {
            close(tcp_fd);
        }
        unlink(UNIX_SOCKET_PATH);
        return EXIT_FAILURE;
    }

    printf("Servidor Ex4: UNIX=%s TCP porto=%lu pool=%zu workers\n",
           UNIX_SOCKET_PATH, port, pool_workers);

    serve_loop(unix_fd, tcp_fd);

    close(unix_fd);
    close(tcp_fd);
    unlink(UNIX_SOCKET_PATH);

    pthread_join(stats_thread, NULL);
    if (threadpool_destroy(&g_pool) != 0) {
        perror("threadpool_destroy");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
