#define _XOPEN_SOURCE 600

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_VALUES ((unsigned long)6e8)

typedef struct {
    long sum;
    short min;
    short max;
} PartialResult;

static void fatal_system_error(const char *error_msg) {
    perror(error_msg);
    exit(EXIT_FAILURE);
}

static void random_init(void) {
    srandom(2026); // srandom(time(NULL)); 
}

static long random_get_value(long min, long max) {
    return min + random() % (max - min + 1);
}

static short *vector_create_short(unsigned long dim) {
    return malloc(dim * sizeof(short));
}

static void vector_random_init_short(short values[], unsigned long dim) {
    for (unsigned long i = 0; i < dim; ++i) {
        values[i] = (short)random_get_value(SHRT_MIN, SHRT_MAX);
    }
}

static PartialResult process_chunk(short values[], unsigned long start, unsigned long end) {
    PartialResult result;
    result.sum = 0;
    result.min = SHRT_MAX;  // inicialização do min com o maior valor possível
    result.max = SHRT_MIN;  // inicialização do max com o menor valor possível

    for (unsigned long i = start; i <= end; ++i) {
        short v = values[i];
        result.sum += v;
        if (v < result.min) result.min = v;
        if (v > result.max) result.max = v;
    }
    return result;
}

int main(int argc, char *argv[]) {
    random_init();

    unsigned long dim = MAX_VALUES;
    long workers = 2;   // número de processos filhos 
    if (argc > 1) {
        dim = strtoul(argv[1], NULL, 10);
    }
    if (argc > 2) {
        workers = strtol(argv[2], NULL, 10);
    }
    if (argc != 3 || dim == 0 || workers <= 0) {
        fprintf(stderr, "Uso: %s <dimensao_vetor> <num_processos>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Creating a vector of %lu (%.2f MB; %.2f GB) values\n", dim, dim / 1e6, dim / 1e9);
    printf("This will require approximately %.2f MB (%.2f GB) of memory\n",
           dim * sizeof(short) / 1e6,
           dim * sizeof(short) / 1e9);

    short *values = vector_create_short(dim);
    if (values == NULL) {
        fprintf(stderr, "Falha na alocacao de memoria para %lu valores\n", dim);
        return EXIT_FAILURE;
    }
    vector_random_init_short(values, dim);

    // ##### Inicialização dos pipes e dos IDs de processos filhos #####
    // calloc: alocação de memória e inicialização com 0
    int *pipe_read = calloc((size_t)workers, sizeof(int)); // array de descritores de ficheiros para a leitura
    pid_t *pids = calloc((size_t)workers, sizeof(pid_t));  // array de IDs de processos filhos
    if (pipe_read == NULL || pids == NULL) {
        free(values);
        free(pipe_read);
        free(pids);
        fprintf(stderr, "Falha na alocacao de estruturas auxiliares\n");
        return EXIT_FAILURE;
    }

    // ##### Inicialização do tempo de início e fim #####
    struct timespec t_start;
    struct timespec t_end;
    if (clock_gettime(CLOCK_MONOTONIC, &t_start) != 0) {
        fatal_system_error("clock_gettime start");
    }

    unsigned long base = dim / (unsigned long)workers;
    unsigned long remainder = dim % (unsigned long)workers;
    unsigned long current_start = 0;

    // ##### Criação dos processos filhos e distribuição dos intervalos do vetor #####
    for (long i = 0; i < workers; ++i) {
        int pipe_fd[2];
        if (pipe(pipe_fd) < 0) { // criação da pipe e tratamento de erros
            fatal_system_error("pipe");
        }

        unsigned long chunk_size = base + ((unsigned long)i < remainder ? 1UL : 0UL); // tamanho do intervalo do vetor para o processo filho + resto -> +1UL para o primeiro processo filho atual até ao resto ser 0 (ex: 100/3 = 33.33 -> 33 + 1 = 34)
        unsigned long chunk_start = current_start;
        unsigned long chunk_end = (chunk_size == 0) ? chunk_start : (chunk_start + chunk_size - 1);
        current_start += chunk_size;

        pid_t pid = fork(); // criação do processo filho e tratamento de erros
        if (pid < 0) {
            fatal_system_error("fork");
        }
        // ##### Código que é executado pelo processo filho #####
        if (pid == 0) { 
            if (close(pipe_fd[0]) < 0) { // fecho do descritor de ficheiro para a leitura e tratamento de erros
                fatal_system_error("close read end child");
            }

            PartialResult part; // resultado parcial
            if (chunk_size == 0) {
                part.sum = 0; // inicialização da soma com 0
                part.min = SHRT_MAX; // inicialização do min com o maior valor possível
                part.max = SHRT_MIN; // inicialização do max com o menor valor possível
            } else {
                part = process_chunk(values, chunk_start, chunk_end);
            }

            ssize_t wr = write(pipe_fd[1], &part, sizeof(part)); // escrita do resultado parcial na pipe e tratamento de erros
            if (wr != (ssize_t)sizeof(part)) {
                fatal_system_error("write partial result");
            }

            if (close(pipe_fd[1]) < 0) { // fecho do descritor de ficheiro para a escrita e tratamento de erros
                fatal_system_error("close write end child");
            }
            free(values);       // liberação da memória do vetor
            free(pipe_read);    // liberação da memória do array de descritores de ficheiros para a leitura -> pipe_read não é necessário para o processo filho
            free(pids);         // liberação da memória do array de IDs de processos filhos -> pids não é necessário para o processo filho
            _exit(EXIT_SUCCESS);// saída do processo filho
        }

        // ##### Código que é executado pelo processo pai #####
        pids[i] = pid;
        pipe_read[i] = pipe_fd[0]; // atribuição do descritor de ficheiro para a leitura ao array de descritores de ficheiros para a leitura
        if (close(pipe_fd[1]) < 0) { // fecho do descritor de ficheiro para a escrita e tratamento de erros
            fatal_system_error("close write end parent");
        }
    }

    long total_sum = 0;
    short total_min = SHRT_MAX;
    short total_max = SHRT_MIN;

    // ##### Leitura dos resultados parciais dos processos filhos (Executado pelo processo pai) #####
    for (long i = 0; i < workers; ++i) {
        PartialResult part;
        ssize_t rd = read(pipe_read[i], &part, sizeof(part));
        if (rd < 0) {
            fatal_system_error("read partial result");
        }
        if (rd != (ssize_t)sizeof(part)) {
            fprintf(stderr, "Leitura incompleta do resultado parcial\n");
            return EXIT_FAILURE;
        }
        if (close(pipe_read[i]) < 0) {
            fatal_system_error("close read end parent");
        }

        total_sum += part.sum;
        if (part.min < total_min) total_min = part.min;
        if (part.max > total_max) total_max = part.max;
    }

    // ##### Espera pelos processos filhos terminarem (Executado pelo processo pai) #####
    for (long i = 0; i < workers; ++i) {
        int status = 0;
        if (waitpid(pids[i], &status, 0) < 0) {
            fatal_system_error("waitpid child");
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Processo filho %ld terminou com erro\n", (long)pids[i]);
            return EXIT_FAILURE;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &t_end) != 0) {
        fatal_system_error("clock_gettime end");
    }

    double elapsed_time =
        (t_end.tv_sec - t_start.tv_sec) + (t_end.tv_nsec - t_start.tv_nsec) * 1e-9;

    printf("smaller is %d\n", total_min);
    printf("bigger  is %d\n", total_max);
    printf("The sum is %ld\n", total_sum);
    printf("Total elapsed time = %9.6lfs (%9.6lfus)\n",
           elapsed_time,
           elapsed_time * 1e6);

    free(values);
    free(pipe_read);
    free(pids);
    return EXIT_SUCCESS;
}
