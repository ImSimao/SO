#define _POSIX_C_SOURCE 200809L

#include "threadpool.h"

#include <errno.h>
#include <stdlib.h>

typedef struct work_item {    // struct que define um trabalho
    threadpool_routine_fn routine;    // função para o thread worker -> executa os trabalhos da queue
    void *arg;    // argumento para a função
    int terminate;    // sinaliza que o trabalho é um trabalho de terminação
    struct work_item *next;    // ponteiro para o próximo trabalho da queue
} work_item_t;

static work_item_t *pool_head(threadpool_t *pool) {    // retorna o primeiro trabalho da queue
    return (work_item_t *)pool->head;
}

static work_item_t *pool_tail(threadpool_t *pool) {    // retorna o último trabalho da queue
    return (work_item_t *)pool->tail;
}

static void set_pool_head(threadpool_t *pool, work_item_t *item) {    // define o primeiro trabalho da queue
    pool->head = item;
}

static void set_pool_tail(threadpool_t *pool, work_item_t *item) {    // define o último trabalho da queue
    pool->tail = item;
}

static work_item_t *make_terminate_item(void) {    // cria um trabalho de terminação
    work_item_t *item = malloc(sizeof(*item));
    if (item == NULL) {
        return NULL;
    }

    item->routine = NULL;
    item->arg = NULL;
    item->terminate = 1;
    item->next = NULL;
    return item;
}

static int enqueue_work(threadpool_t *pool, work_item_t *item) {    // Adiciona trabalhos a queue
    work_item_t *tail = pool_tail(pool);

    if (tail != NULL) {    // se a queue não estiver vazia, adiciona o trabalho ao final da queue
        tail->next = item;
    } else {
        set_pool_head(pool, item);    // se a queue estiver vazia, define o primeiro trabalho da queue
    }
    set_pool_tail(pool, item);    // define o último trabalho da queue
    pool->queue_size++;   
    pthread_cond_signal(&pool->cond);    // sinaliza a chegada de trabalhos
    return 0;
}

static void signal_destroy_waiters(threadpool_t *pool) {    // sinaliza os threads que estão a esperar pela destruição do pool
    if (pool->shutdown && pool->queue_size == 0 && pool->active_jobs == 0) {
        pthread_cond_broadcast(&pool->cond_destroy);
    }
}

static int enqueue_terminate_items(threadpool_t *pool, size_t count) {    // termina os threads do pool
    for (size_t i = 0; i < count; ++i) {
        work_item_t *item = make_terminate_item();
        if (item == NULL) {
            return -1;
        }
        enqueue_work(pool, item);    // adiciona o trabalho de terminação à queue
    }
    return 0;
}

static void *worker_thread(void *arg) {    // Thread worker -> executa os trabalhos da queue
    threadpool_t *pool = (threadpool_t *)arg;

    for (;;) {    // loop infinito -> executa os trabalhos da queue
        pthread_mutex_lock(&pool->mutex);   

        while (pool_head(pool) == NULL) {    // espera pela chegada de trabalhos
            if (pthread_cond_wait(&pool->cond, &pool->mutex) != 0) {   
                pthread_mutex_unlock(&pool->mutex); 
                return NULL;
            }
        }

        work_item_t *item = pool_head(pool);    // busca o primeiro trabalho da queue
        set_pool_head(pool, item->next);
        if (pool_head(pool) == NULL) {    // se a queue estiver vazia, limpa a tail
            set_pool_tail(pool, NULL);
        }
        pool->queue_size--;

        if (item->terminate) {    // se o trabalho é um trabalho de terminação, sinaliza os threads que estão a esperar pela destruição do pool
            signal_destroy_waiters(pool);
            pthread_mutex_unlock(&pool->mutex);
            free(item);
            return NULL;
        }

        pool->active_jobs++;
        pthread_mutex_unlock(&pool->mutex); 

        item->routine(item->arg);    // executa o trabalho
        free(item);

        pthread_mutex_lock(&pool->mutex);
        pool->active_jobs--;
        signal_destroy_waiters(pool);
        pthread_mutex_unlock(&pool->mutex);
    }
}

int threadpool_init(threadpool_t *pool, size_t num_workers) {    // Inicializa o pool
    if (pool == NULL || num_workers == 0) {
        errno = EINVAL;
        return -1;
    }

    set_pool_head(pool, NULL);
    set_pool_tail(pool, NULL);
    pool->queue_size = 0;
    pool->active_jobs = 0;
    pool->num_workers = num_workers;
    pool->shutdown = 0;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {    // inicia o mutex
        return -1;
    }
    if (pthread_cond_init(&pool->cond, NULL) != 0) {    // inicia a condição de espera para a chegada de trabalhos
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }
    if (pthread_cond_init(&pool->cond_destroy, NULL) != 0) {    // inicia a condição de espera para a destruição do pool
        pthread_cond_destroy(&pool->cond);
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }

    pool->workers = calloc(num_workers, sizeof(*pool->workers));    // aloca memória para os threads do pool
    if (pool->workers == NULL) {
        pthread_cond_destroy(&pool->cond_destroy);
        pthread_cond_destroy(&pool->cond);
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }

    for (size_t i = 0; i < num_workers; ++i) {    // cria os threads do pool
        if (pthread_create(&pool->workers[i], NULL, worker_thread, pool) != 0) {
            if (pthread_mutex_lock(&pool->mutex) != 0) {
                return -1;
            }
            pool->shutdown = 1;
            if (enqueue_terminate_items(pool, i) != 0) {
                pthread_mutex_unlock(&pool->mutex);
                return -1;
            }
            pthread_mutex_unlock(&pool->mutex);

            for (size_t j = 0; j < i; ++j) {
                pthread_join(pool->workers[j], NULL);
            }

            free(pool->workers);
            pool->workers = NULL;
            pthread_cond_destroy(&pool->cond_destroy);
            pthread_cond_destroy(&pool->cond);
            pthread_mutex_destroy(&pool->mutex);
            return -1;
        }
    }

    return 0;
}

int threadpool_submit(threadpool_t *pool, threadpool_routine_fn routine, void *arg) {    // Adiciona um trabalho ao pool
    if (pool == NULL || routine == NULL) {
        errno = EINVAL;
        return -1;
    }

    work_item_t *item = malloc(sizeof(*item));
    if (item == NULL) {
        return -1;
    }

    item->routine = routine;
    item->arg = arg;
    item->terminate = 0;
    item->next = NULL;

    if (pthread_mutex_lock(&pool->mutex) != 0) {
        free(item);
        return -1;
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        free(item);
        errno = EINVAL;
        return -1;
    }

    enqueue_work(pool, item);
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

int threadpool_destroy(threadpool_t *pool) {    // Destrói o pool
    if (pool == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (pthread_mutex_lock(&pool->mutex) != 0) {
        return -1;
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        errno = EINVAL;
        return -1;
    }

    pool->shutdown = 1;

    while (pool->queue_size > 0 || pool->active_jobs > 0) {
        if (pthread_cond_wait(&pool->cond_destroy, &pool->mutex) != 0) {
            pthread_mutex_unlock(&pool->mutex);
            return -1;
        }
    }

    if (enqueue_terminate_items(pool, pool->num_workers) != 0) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    pthread_mutex_unlock(&pool->mutex);

    for (size_t i = 0; i < pool->num_workers; ++i) {
        if (pthread_join(pool->workers[i], NULL) != 0) {
            return -1;
        }
    }

    free(pool->workers);
    pool->workers = NULL;

    pthread_cond_destroy(&pool->cond_destroy);
    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->mutex);
    return 0;
}
