#define _POSIX_C_SOURCE 200809L

#include "../Ex2/threadpool.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    long long sum;
    int16_t min;
    int16_t max;
} Stats;

typedef struct {
    const int16_t *values;
    size_t start;
    size_t end;
    Stats out;
} ThreadTask;

typedef struct {
    const int16_t *values;
    size_t start;
    size_t end;
    Stats *out;
    pthread_mutex_t *mutex;
    size_t *remaining;
    pthread_cond_t *cond;
} PoolTask;

static void fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static double now_seconds(void) { // get tempo em segundos
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fatal("clock_gettime");
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int16_t *random_vector(size_t n) { // cria um vetor de inteiros de 16 bits com valores aleatórios
    int16_t *v = malloc(n * sizeof(*v));
    if (v == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < n; ++i) {
        v[i] = (int16_t)(rand() % 65536 - 32768);    // valores aleatórios entre -32768 e 32767 -> (65536/2 = 32768) deslocamento
    }
    return v;
}

static Stats calc_seq(const int16_t *values, size_t n) { // calcula os stats sequencialmente -> max, min, sum
    Stats s = {0, INT16_MAX, INT16_MIN};
    for (size_t i = 0; i < n; ++i) {
        int16_t x = values[i];
        s.sum += x;
        if (x < s.min) {
            s.min = x;
        }
        if (x > s.max) {
            s.max = x;
        }
    }
    return s;
}

static Stats calc_range(const int16_t *values, size_t begin, size_t end) { // calcula os stats para um intervalo do vetor -> max, min, sum
    Stats s = {0, INT16_MAX, INT16_MIN};
    for (size_t i = begin; i < end; ++i) {
        int16_t x = values[i];
        s.sum += x;
        if (x < s.min) {
            s.min = x;
        }
        if (x > s.max) {
            s.max = x;
        }
    }
    return s;
}

static Stats merge_stats(const Stats *parts, size_t count) { // merge dos stats -> max, min, sum (nos workers)
    Stats out = {0, INT16_MAX, INT16_MIN};
    for (size_t i = 0; i < count; ++i) {
        out.sum += parts[i].sum;
        if (parts[i].min < out.min) {
            out.min = parts[i].min;
        }
        if (parts[i].max > out.max) {
            out.max = parts[i].max;
        }
    }
    return out;
}

static void split_work(size_t n, size_t workers, size_t index, size_t *begin, size_t *end) { // divide o trabalho entre os workers -> max, min, sum
    size_t base = n / workers;
    size_t rem = n % workers;
    size_t len = base + (index < rem ? 1U : 0U);
    size_t cursor = 0;

    for (size_t i = 0; i < index; ++i) {
        cursor += base + (i < rem ? 1U : 0U);
    }

    *begin = cursor;
    *end = cursor + len;
}

static void *worker_fn(void *arg) { // função para o thread worker -> calcula os stats para um intervalo do vetor 
    ThreadTask *t = (ThreadTask *)arg;
    t->out = calc_range(t->values, t->start, t->end);
    return NULL;
}

static Stats calc_threads(const int16_t *values, size_t n, size_t workers) { // calcula os stats para um vetor com threads -> max, min, sum
    if (workers == 0)  workers = 1;
    if (workers > n) workers = n;
    

    pthread_t *ids = calloc(workers, sizeof(*ids));    // alocação de memória para os IDs das threads
    ThreadTask *tasks = calloc(workers, sizeof(*tasks));    // alocação de memória para as tasks das threads
    if (ids == NULL || tasks == NULL) {    // verifica se a alocação de memória foi bem-sucedida
        free(ids);
        free(tasks);
        fatal("calloc");    
    }

    for (size_t i = 0; i < workers; ++i) {   // loop para criar as threads
        tasks[i].values = values;
        split_work(n, workers, i, &tasks[i].start, &tasks[i].end);    // divide o trabalho entre os workers
        if (pthread_create(&ids[i], NULL, worker_fn, &tasks[i]) != 0) {    // cria a thread 
            fatal("pthread_create");
        }
    }

    for (size_t i = 0; i < workers; ++i) {    // loop para esperar as threads terminarem
        if (pthread_join(ids[i], NULL) != 0) {
            fatal("pthread_join");
        }
    }

    Stats *parts = calloc(workers, sizeof(*parts));    // alocação de memória para as partes dos stats
    if (parts == NULL) {
        fatal("calloc");    
    }
    for (size_t i = 0; i < workers; ++i) {    
        parts[i] = tasks[i].out;    // atribui as partes dos stats
    }

    Stats out = merge_stats(parts, workers);    // merge das partes dos stats
    free(parts);
    free(ids);
    free(tasks);
    return out;
}

static Stats calc_processes(const int16_t *values, size_t n, size_t workers) { // calcula os stats para um vetor com processos -> max, min, sum
    if (workers == 0) workers = 1;
    if (workers > n) workers = n;
    

    int *read_ends = calloc(workers, sizeof(*read_ends));    // alocação de memória para os descritores de ficheiros para a leitura
    pid_t *pids = calloc(workers, sizeof(*pids));    // alocação de memória para os IDs dos processos filhos
    if (read_ends == NULL || pids == NULL) {
        free(read_ends);
        free(pids);
        fatal("calloc");
    }

    for (size_t i = 0; i < workers; ++i) {   // loop para criar os processos filhos
        int fd[2];    // PIPE para a leitura e escrita
        if (pipe(fd) != 0) {    // cria a pipe 
            fatal("pipe");
        }

        size_t begin = 0;
        size_t end = 0;
        split_work(n, workers, i, &begin, &end);    // divide o trabalho entre os processos filhos

        pid_t pid = fork();    // cria o processo filho 
        if (pid < 0) {
            fatal("fork");
        }
        if (pid == 0) {    // processo filho
            close(fd[0]);
            Stats s = calc_range(values, begin, end);
            ssize_t wr = write(fd[1], &s, sizeof(s));
            close(fd[1]);
            _exit(wr == (ssize_t)sizeof(s) ? 0 : 1);
        }

        close(fd[1]);
        read_ends[i] = fd[0];
        pids[i] = pid;
    }

    Stats *parts = calloc(workers, sizeof(*parts));    // alocação de memória para as partes dos stats
    if (parts == NULL) {
        fatal("calloc");
    }

    for (size_t i = 0; i < workers; ++i) {    // loop para ler os resultados dos processos filhos
        ssize_t rd = read(read_ends[i], &parts[i], sizeof(parts[i]));    // lê o resultado do processo filho
        close(read_ends[i]);
        if (rd != (ssize_t)sizeof(parts[i])) {
            fatal("read child result");
        }
    }

    for (size_t i = 0; i < workers; ++i) {    // loop para esperar os processos filhos terminarem
        int st = 0;
        if (waitpid(pids[i], &st, 0) < 0) {
            fatal("waitpid");
        }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            fprintf(stderr, "child %ld exited with error\n", (long)pids[i]);
            exit(EXIT_FAILURE);
        }
    }

    Stats out = merge_stats(parts, workers);    // merge das partes dos stats
    free(parts);
    free(read_ends);
    free(pids);
    return out;
}

static void pool_worker(void *arg) { // função para o thread poolworker -> calcula os stats para um intervalo do vetor 
    PoolTask *task = (PoolTask *)arg;

    *task->out = calc_range(task->values, task->start, task->end);

    pthread_mutex_lock(task->mutex);
    (*task->remaining)--;
    if (*task->remaining == 0) {
        pthread_cond_broadcast(task->cond);    // sinaliza que todos os trabalhos foram completados
    }
    pthread_mutex_unlock(task->mutex);
}

static Stats calc_threadpool(const int16_t *values, size_t n, size_t workers) { // calcula os stats para um vetor com threadpool -> max, min, sum
    if (workers == 0) workers = 1;
    if (workers > n) workers = n;

    threadpool_t pool;    // threadpool
    PoolTask *tasks = calloc(workers, sizeof(*tasks));    // alocação de memória para as tasks do threadpool
    Stats *parts = calloc(workers, sizeof(*parts));    // alocação de memória para as partes dos stats
    if (tasks == NULL || parts == NULL) {
        free(tasks);
        free(parts);
        fatal("calloc");
    }

    pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;    // mutex para sincronização; PTHREAD_MUTEX_INITIALIZER inicializa o mutex com valores padrão
    pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;    // condição de espera para sincronização; PTHREAD_COND_INITIALIZER inicializa a condição de espera com valores padrão
    size_t remaining = workers;

    if (threadpool_init(&pool, workers) != 0) {    // inicializa o threadpool 
        fatal("threadpool_init");
    }

    for (size_t i = 0; i < workers; ++i) {    // loop para criar as tasks do threadpool
        tasks[i].values = values;
        split_work(n, workers, i, &tasks[i].start, &tasks[i].end);    // divide o trabalho entre os workers
        tasks[i].out = &parts[i];
        tasks[i].mutex = &done_mutex;    // mutex para sincronização
        tasks[i].remaining = &remaining;    // número de trabalhos restantes
        tasks[i].cond = &done_cond;

        if (threadpool_submit(&pool, pool_worker, &tasks[i]) != 0) {    // adiciona a task ao threadpool 
            fatal("threadpool_submit");
        }
    }

    pthread_mutex_lock(&done_mutex);    // bloqueia o mutex para sincronização
    while (remaining > 0) {    // loop para esperar as tasks terminarem
        pthread_cond_wait(&done_cond, &done_mutex);
    }
    pthread_mutex_unlock(&done_mutex);

    if (threadpool_destroy(&pool) != 0) {    // destrói o threadpool 
        fatal("threadpool_destroy");
    }

    Stats out = merge_stats(parts, workers);    // merge das partes dos stats
    free(tasks);
    free(parts);
    return out;
}

static void print_stats(const char *label, const Stats *s, double elapsed) { // função para imprimir os stats -> min, max, sum e tempo
    printf("%-12s min=%d max=%d sum=%lld time=%.6fs\n", label, (int)s->min, (int)s->max,
           s->sum, elapsed);
}

static int stats_equal(const Stats *a, const Stats *b) { // verifica se os stats são iguais -> max, min, sum
    return a->sum == b->sum && a->min == b->min && a->max == b->max;
}

int main(int argc, char **argv) {
    size_t n = 1000000U;
    size_t workers = 4U;

    if (argc >= 2) {
        n = strtoull(argv[1], NULL, 10);    // converte o argumento para um número inteiro
    }
    if (argc >= 3) {
        workers = strtoull(argv[2], NULL, 10);    // converte o argumento para um número inteiro
    }
    if (n == 0U || workers == 0U) {
        fprintf(stderr, "uso: %s <num_elementos> <num_workers>\n", argv[0]);    // imprime o uso do programa
        return EXIT_FAILURE;
    }

    srand(2026);
    int16_t *values = random_vector(n); // cria um vetor de inteiros de 16 bits com valores aleatórios
    if (values == NULL) {
        fatal("malloc vector");
    }

    double t0 = now_seconds();
    Stats s_seq = calc_seq(values, n); // calcula os stats para um vetor sequencial
    double t1 = now_seconds();

    Stats s_proc = calc_processes(values, n, workers); // calcula os stats para um vetor com processos
    double t2 = now_seconds();

    Stats s_thr = calc_threads(values, n, workers); // calcula os stats para um vetor com threads
    double t3 = now_seconds();

    Stats s_pool = calc_threadpool(values, n, workers); // calcula os stats para um vetor com threadpool
    double t4 = now_seconds();

    print_stats("sequencial", &s_seq, t1 - t0);
    print_stats("processos", &s_proc, t2 - t1);
    print_stats("threads", &s_thr, t3 - t2);
    print_stats("threadpool", &s_pool, t4 - t3);

    if (!stats_equal(&s_seq, &s_proc) || !stats_equal(&s_seq, &s_thr) ||
        !stats_equal(&s_seq, &s_pool)) {    // verifica se os stats são iguais (stats_equal retorna 1 se os stats são iguais, 0 caso contrário) logo !stats_equal com stats_equal=1 fica =0 logo falso logo não ocorreu erro
        fprintf(stderr, "ERRO: resultados diferentes entre versoes\n");
        free(values);
        return EXIT_FAILURE;
    }

    free(values);
    return EXIT_SUCCESS;
}
