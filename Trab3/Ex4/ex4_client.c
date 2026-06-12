#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
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
    const char *mode;
    const char *host;
    const char *port;
    size_t n;
    size_t idx;
} ClientTask;

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

static uint64_t ntohll(uint64_t v) {
    uint32_t lo = ntohl((uint32_t)(v >> 32));
    uint32_t hi = ntohl((uint32_t)(v & 0xFFFFFFFFU));
    return ((uint64_t)hi << 32) | lo;
}

static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static int connect_unix(const char *path) {
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

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect AF_UNIX");
        close(fd);
        return -1;
    }
    return fd;
}

static int write_frame(int fd, const void *data, uint32_t len) {
    uint32_t be = htonl(len);
    if (write_full(fd, &be, sizeof(be)) != 0) {
        return -1;
    }
    if (len > 0 && write_full(fd, data, len) != 0) {
        return -1;
    }
    return 0;
}

static int read_error_message(int fd, char *buf, size_t cap) {
    uint32_t len_be = 0;
    if (read_full(fd, &len_be, sizeof(len_be)) <= 0) {
        return -1;
    }
    uint32_t len = ntohl(len_be);
    if (len + 1U > cap) {
        len = (uint32_t)(cap - 1U);
    }
    if (read_full(fd, buf, len) <= 0) {
        return -1;
    }
    buf[len] = '\0';
    return 0;
}

static int make_request(const ClientTask *task) {
    int fd = -1;
    if (strcmp(task->mode, "unix") == 0) {
        fd = connect_unix(UNIX_SOCKET_PATH);
    } else if (strcmp(task->mode, "tcp") == 0) {
        fd = connect_tcp(task->host, task->port);
    } else {
        fprintf(stderr, "[%zu] modo invalido\n", task->idx);
        return -1;
    }

    if (fd < 0) {
        fprintf(stderr, "[%zu] erro a ligar ao servidor\n", task->idx);
        return -1;
    }

    uint16_t *values = malloc(task->n * sizeof(*values));
    if (values == NULL) {
        close(fd);
        return -1;
    }
    for (size_t i = 0; i < task->n; ++i) {
        values[i] = (uint16_t)(rand() & 0xFFFFU);
    }

    uint16_t *be = malloc(task->n * sizeof(*be));
    if (be == NULL) {
        free(values);
        close(fd);
        return -1;
    }
    for (size_t i = 0; i < task->n; ++i) {
        be[i] = htons(values[i]);
    }

    if (write_frame(fd, be, (uint32_t)(task->n * sizeof(uint16_t))) != 0 ||
        write_frame(fd, NULL, 0U) != 0) {
        fprintf(stderr, "[%zu] erro ao enviar pedido\n", task->idx);
        free(be);
        free(values);
        close(fd);
        return -1;
    }

    uint8_t st = 0;
    if (read_full(fd, &st, 1U) <= 0) {
        fprintf(stderr, "[%zu] resposta truncada\n", task->idx);
        free(be);
        free(values);
        close(fd);
        return -1;
    }

    if (st == ST_OK) {
        uint16_t min_be = 0;
        uint16_t max_be = 0;
        uint64_t sum_be = 0;
        if (read_full(fd, &min_be, sizeof(min_be)) <= 0 ||
            read_full(fd, &max_be, sizeof(max_be)) <= 0 ||
            read_full(fd, &sum_be, sizeof(sum_be)) <= 0) {
            fprintf(stderr, "[%zu] resposta ok truncada\n", task->idx);
            free(be);
            free(values);
            close(fd);
            return -1;
        }
        uint16_t min = ntohs(min_be);
        uint16_t max = ntohs(max_be);
        uint64_t sum = ntohll(sum_be);
        printf("[%zu] %s ok: min=%u max=%u sum=%llu\n", task->idx, task->mode,
               (unsigned)min, (unsigned)max, (unsigned long long)sum);
    } else {
        char msg[256];
        if (read_error_message(fd, msg, sizeof(msg)) != 0) {
            snprintf(msg, sizeof(msg), "(mensagem indisponivel)");
        }
        fprintf(stderr, "[%zu] %s erro status=%u: %s\n", task->idx, task->mode,
                (unsigned)st, msg);
        free(be);
        free(values);
        close(fd);
        return -1;
    }

    free(be);
    free(values);
    close(fd);
    return 0;
}

static void *task_fn(void *arg) {
    ClientTask *task = (ClientTask *)arg;
    (void)make_request(task);
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "uso: %s tcp <ip|host> <porto> <num_elementos> [num_ligacoes]\n"
            "     %s unix <num_elementos> [num_ligacoes]\n",
            prog, prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *mode = argv[1];
    const char *host = NULL;
    const char *port = NULL;
    size_t n = 0;
    size_t links = 1U;
    int base = 0;

    if (strcmp(mode, "tcp") == 0) {
        if (argc < 5) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        host = argv[2];
        port = argv[3];
        n = strtoull(argv[4], NULL, 10);
        base = 5;
    } else if (strcmp(mode, "unix") == 0) {
        if (argc < 3) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        n = strtoull(argv[2], NULL, 10);
        base = 3;
    } else {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (base < argc) {
        links = strtoull(argv[base], NULL, 10);
    }

    if (n == 0U || links == 0U) {
        fprintf(stderr, "argumentos invalidos\n");
        return EXIT_FAILURE;
    }

    srand(2026);

    if (links == 1U) {
        ClientTask task = {.mode = mode, .host = host, .port = port, .n = n, .idx = 0U};
        return make_request(&task) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    pthread_t *ids = calloc(links, sizeof(*ids));
    ClientTask *tasks = calloc(links, sizeof(*tasks));
    if (ids == NULL || tasks == NULL) {
        free(ids);
        free(tasks);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < links; ++i) {
        tasks[i].mode = mode;
        tasks[i].host = host;
        tasks[i].port = port;
        tasks[i].n = n;
        tasks[i].idx = i;
        if (pthread_create(&ids[i], NULL, task_fn, &tasks[i]) != 0) {
            fprintf(stderr, "pthread_create falhou\n");
            free(ids);
            free(tasks);
            return EXIT_FAILURE;
        }
    }

    for (size_t i = 0; i < links; ++i) {
        (void)pthread_join(ids[i], NULL);
    }

    free(ids);
    free(tasks);
    return EXIT_SUCCESS;
}
