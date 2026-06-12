#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "threadpool.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NUM_WORKERS 4
#define NUM_JOBS 20

typedef struct {
    int id;
    int *counter;
    pthread_mutex_t *mutex;
} job_arg_t;

static void simple_job(void *arg) { // função para o thread pool worker -> executa o trabalho simples
    job_arg_t *job = (job_arg_t *)arg;

    usleep(10000);  // espera 10ms

    pthread_mutex_lock(job->mutex);    // bloqueia o mutex para sincronização
    (*job->counter)++;
    printf("Trabalho %d executado (total=%d)\n", job->id, *job->counter);
    pthread_mutex_unlock(job->mutex);    // liberta o mutex para sincronização

    free(job);
}

static void slow_job(void *arg) { // função para o thread pool worker -> executa o trabalho lento
    int *id = (int *)arg;
    usleep(200000);  // espera 200ms
    printf("Trabalho lento %d concluido\n", *id);
    free(id);    // liberta a memória alocada para o id
}

int main(void) {
    threadpool_t pool;    // threadpool
    int counter = 0;
    pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;    // mutex para sincronização   

    printf("=== Teste 1: submissao e execucao de trabalhos ===\n");
    if (threadpool_init(&pool, NUM_WORKERS) != 0) {
        perror("threadpool_init");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < NUM_JOBS; ++i) {   // loop para criar os trabalhos
        job_arg_t *arg = malloc(sizeof(*arg));
        if (arg == NULL) {
            fprintf(stderr, "malloc falhou\n");
            return EXIT_FAILURE;
        }

        arg->id = i;
        arg->counter = &counter;
        arg->mutex = &counter_mutex;

        if (threadpool_submit(&pool, simple_job, arg) != 0) {   // adiciona o trabalho ao threadpool
            perror("threadpool_submit");
            free(arg);
            return EXIT_FAILURE;
        }
    }

    printf("=== Teste 2: destroy aguarda trabalhos pendentes ===\n");
    for (int i = 0; i < 3; ++i) {   // loop para criar os trabalhos lentos
        int *id = malloc(sizeof(*id));
        if (id == NULL) {
            fprintf(stderr, "malloc falhou\n");
            return EXIT_FAILURE;
        }
        *id = i;
        if (threadpool_submit(&pool, slow_job, id) != 0) {   // adiciona o trabalho lento ao threadpool
            perror("threadpool_submit slow");
            free(id);
            return EXIT_FAILURE;
        }
    }

    if (threadpool_destroy(&pool) != 0) {   // destrói o threadpool
        perror("threadpool_destroy");
        return EXIT_FAILURE;
    }
    printf("threadpool_destroy concluido (workers terminados)\n");

    if (counter != NUM_JOBS) {   // verifica se o número de trabalhos executados é igual ao número de trabalhos esperados
        fprintf(stderr, "Erro: esperados %d trabalhos, executados %d\n", NUM_JOBS, counter);
        return EXIT_FAILURE;
    }

    printf("=== Teste 3: submit apos destroy deve falhar ===\n");
    if (threadpool_submit(&pool, simple_job, NULL) == 0) {   // adiciona um trabalho ao threadpool
        fprintf(stderr, "Erro: submit apos destroy deveria falhar\n");
        return EXIT_FAILURE;
    }
    printf("submit apos destroy: erro (esperado)\n");

    printf("=== Teste 4: destroy duplicado deve falhar ===\n");
    if (threadpool_destroy(&pool) == 0) {   // destrói o threadpool
        fprintf(stderr, "Erro: segundo destroy deveria falhar\n");
        return EXIT_FAILURE;
    }
    printf("segundo destroy: erro (esperado)\n");

    printf("\nTodos os testes passaram.\n");
    return EXIT_SUCCESS;
}
