#include "countdown.h"

#include <errno.h>

int countdown_init(countdown_t *cd, int initialValue) {    // Inicializa o countdown
    if (cd == NULL || initialValue <= 0) {    // se o countdown ou o valor inicial for nulo ou menor que 0, retorna erro
        errno = EINVAL;
        return -1;
    }

    cd->count = initialValue;   
    cd->finished = 0;

    if (pthread_mutex_init(&cd->mutex, NULL) != 0) {    // inicia o mutex -> garante acesso exclusivo aos recursos compartilhados
        return -1;
    }
    if (pthread_cond_init(&cd->cond, NULL) != 0) {    // inicia a condição de espera
        pthread_mutex_destroy(&cd->mutex);
        return -1;
    }

    return 0;
}

int countdown_destroy(countdown_t *cd) {  // destrói o countdown
    if (cd == NULL) { // erro de argumento inválido 
        errno = EINVAL;
        return -1;
    }

    pthread_cond_destroy(&cd->cond);    // destrói a condição de espera
    pthread_mutex_destroy(&cd->mutex);    // destrói o mutex
    return 0;
}

int countdown_wait(countdown_t *cd) {    // espera pelo countdown 
    if (cd == NULL) {
        errno = EINVAL; // erro de argumento inválido -> countdown ou mutex nulo
        return -1;
    }

    if (pthread_mutex_lock(&cd->mutex) != 0) {    // bloqueia o mutex -> erro ao bloquear o mutex
        return -1;
    }

    if (cd->finished) {    // se o countdown estiver terminado, liberta o mutex e retorna erro
        pthread_mutex_unlock(&cd->mutex);
        errno = EINVAL; // erro de argumento inválido -> countdown terminado
        return -1;
    }

    while (cd->count > 0) {
        if (pthread_cond_wait(&cd->cond, &cd->mutex) != 0) {    // espera pela condição de espera
            pthread_mutex_unlock(&cd->mutex);
            return -1;
        }
    }

    pthread_mutex_unlock(&cd->mutex);    // liberta o mutex
    return 0;
}

int countdown_down(countdown_t *cd) {    // decrementa o countdown
    if (cd == NULL) {
        errno = EINVAL; // erro de argumento inválido -> countdown nulo
        return -1;
    }

    if (pthread_mutex_lock(&cd->mutex) != 0) {    // bloqueia o mutex -> erro ao bloquear o mutex
        return -1;
    }

    if (cd->finished || cd->count <= 0) {    // se o countdown estiver terminado ou o valor for menor que 0, liberta o mutex e retorna erro
        pthread_mutex_unlock(&cd->mutex);
        errno = EINVAL;
        return -1;
    }

    cd->count--;
    if (cd->count == 0) {
        cd->finished = 1;    // sinaliza que o countdown terminou
        pthread_cond_broadcast(&cd->cond);
    }

    pthread_mutex_unlock(&cd->mutex);    // liberta o mutex
    return 0;
}
