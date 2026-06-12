#ifndef COUNTDOWN_H
#define COUNTDOWN_H

#include <pthread.h>

typedef struct {    // estrutura do countdown
    pthread_mutex_t mutex;    // mutex para garantir acesso exclusivo aos recursos compartilhados
    pthread_cond_t cond;    // condição de espera
    int count;    // valor inicial para o countdown
    int finished;    // sinaliza que o countdown terminou
} countdown_t;

int countdown_init(countdown_t *cd, int initialValue);
int countdown_destroy(countdown_t *cd);
int countdown_wait(countdown_t *cd);
int countdown_down(countdown_t *cd);

#endif
