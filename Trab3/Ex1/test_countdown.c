#include "countdown.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    int id;
    countdown_t *cd;
} thread_arg_t;

static void *waiter_thread(void *arg) {
    thread_arg_t *targ = arg;

    printf("T%d: countdown_wait() - bloqueada\n", targ->id);
    if (countdown_wait(targ->cd) != 0) {
        printf("T%d: countdown_wait() - erro\n", targ->id);
    } else {
        printf("T%d: countdown_wait() - desbloqueada\n", targ->id);
    }

    return NULL;
}

static void *down_thread(void *arg) {
    countdown_t *cd = arg;
    const int downs = 4;

    usleep(500000);   // espera 0.5 segundos

    for (int i = 0; i < downs; ++i) {
        printf("T4: countdown_down() - valor decrementado\n");
        if (countdown_down(cd) != 0) {
            printf("T4: countdown_down() - erro\n");
            break;
        }
    }

    return NULL;
}

int main(void) {
    countdown_t cd;
    pthread_t waiters[4];
    pthread_t downer;
    thread_arg_t args[4];

    if (countdown_init(&cd, 4) != 0) {   // inicializa o countdown 
        perror("countdown_init");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < 4; ++i) {   // loop para criar os threads waiters
        args[i].id = i;
        args[i].cd = &cd;
        if (pthread_create(&waiters[i], NULL, waiter_thread, &args[i]) != 0) {
            perror("pthread_create waiter");
            return EXIT_FAILURE;
        }
    }

    if (pthread_create(&downer, NULL, down_thread, &cd) != 0) {   // cria o thread downer que vai decrementar o countdown
        perror("pthread_create downer");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < 4; ++i) {   // loop para esperar os threads waiters terminarem
        pthread_join(waiters[i], NULL);
    }
    pthread_join(downer, NULL);   // espera pelo thread downer terminar

    printf("Chamada subsequente a countdown_wait(): ");
    if (countdown_wait(&cd) != 0) {   // chama a função countdown_wait
        printf("erro (esperado)\n");
    } else {
        printf("sucesso (inesperado)\n");
    }

    printf("Chamada subsequente a countdown_down(): ");
    if (countdown_down(&cd) != 0) {   // chama a função countdown_down
        printf("erro (esperado)\n");
    } else {
        printf("sucesso (inesperado)\n");
    }

    countdown_destroy(&cd);   // destrói o countdown
    return EXIT_SUCCESS;
}
