#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stddef.h>

typedef void (*threadpool_routine_fn)(void *arg);    // função para o thread worker -> executa os trabalhos da queue

typedef struct threadpool {    // estrutura do threadpool
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t cond_destroy;

    void *head;    // ponteiro para o primeiro trabalho da queue
    void *tail;    // ponteiro para o último trabalho da queue
    size_t queue_size;    // tamanho da queue
    size_t active_jobs;    // número de trabalhos ativos

    size_t num_workers;
    pthread_t *workers;    // ponteiro para os threads do pool
    int shutdown;    // sinaliza que o pool está a ser destruído
} threadpool_t;

int threadpool_init(threadpool_t *pool, size_t num_workers);
int threadpool_submit(threadpool_t *pool, threadpool_routine_fn routine, void *arg);
int threadpool_destroy(threadpool_t *pool);

#endif
