#define _POSIX_C_SOURCE 200809L

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
	v[i] = (int16_t)(rand() % 65536 - 32768);	// valores aleatórios entre -32768 e 32767 -> (65536/2 = 32768) deslocamento
  }
  return v;
}

static Stats calc_seq(const int16_t *values, size_t n) { // calcula os stats sequencialmente -> max, min, sum
  Stats s = {0, INT16_MAX, INT16_MIN};
  for (size_t i = 0; i < n; ++i) {
	int16_t x = values[i];
	s.sum += x;
	if (x < s.min) s.min = x;
	if (x > s.max) s.max = x;
  }
  return s;
}



static Stats calc_range(const int16_t *values, size_t begin, size_t end) { // calcula os stats para um intervalo do vetor -> max, min, sum
  Stats s = {0, INT16_MAX, INT16_MIN};
  for (size_t i = begin; i < end; ++i) {
	int16_t x = values[i];
	s.sum += x;
	if (x < s.min) s.min = x;
	if (x > s.max) s.max = x;
  }
  return s;
}

//Funções Threads--------------------------------
static void *worker_fn(void *arg) { // função para o thread worker -> calcula os stats para um intervalo do vetor
  ThreadTask *t = (ThreadTask *)arg;
  t->out = calc_range(t->values, t->start, t->end);
  return NULL;
}

static Stats calc_threads(const int16_t *values, size_t n, size_t workers) { // calcula os stats para um vetor com threads -> max, min, sum
  if (workers == 0) workers = 1;
  if (workers > n) workers = n;  // cap de workers para o número de elementos do vetor. ex: n=20 workers=25 então workers=20

  pthread_t *ids = calloc(workers, sizeof(*ids));	// alocação de memória para os IDs das threads
  ThreadTask *tasks = calloc(workers, sizeof(*tasks));	// alocação de memória para as tasks das threads
  if (ids == NULL || tasks == NULL) {	// verifica se a alocação de memória foi bem-sucedida
	free(ids);
	free(tasks);
	fatal("calloc");
  }

  size_t base = n / workers;
  size_t rem = n % workers;
  size_t cursor = 0;
  for (size_t i = 0; i < workers; ++i) {  
    size_t len = base + (i < rem ? 1U : 0U);	// tamanho do intervalo do vetor para o thread + resto -> +1UL para o primeiro thread atual até ao resto ser 0 (ex: 100/3 = 33.33 -> 33 + 1 = 34)
    tasks[i].values = values; 
    tasks[i].start = cursor;  
    tasks[i].end = cursor + len;
    cursor += len;
    if (pthread_create(&ids[i], NULL, worker_fn, &tasks[i]) != 0) {	// criação da thread e tratamento de erros
      fatal("pthread_create");
    }
  }

  Stats out = {0, INT16_MAX, INT16_MIN};	// inicialização dos stats com o valor máximo e mínimo possíveis
  for (size_t i = 0; i < workers; ++i) {
	if (pthread_join(ids[i], NULL) != 0) {	// espera pela finalização da thread e tratamento de erros
	  fatal("pthread_join");
	}
	out.sum += tasks[i].out.sum;	// soma dos stats das threads
	if (tasks[i].out.min < out.min) out.min = tasks[i].out.min;
	if (tasks[i].out.max > out.max) out.max = tasks[i].out.max;	// atualização dos stats com os stats das threads
  }

  free(ids);	// liberação da memória alocada para os IDs das threads
  free(tasks);	// liberação da memória alocada para as tasks das threads
  return out;
}

//Funções Processos--------------------------------
static Stats calc_processes(const int16_t *values, size_t n, size_t workers) { // calcula os stats para um vetor com processos -> max, min, sum
  if (workers == 0) workers = 1;
  if (workers > n) workers = n;

  int *read_ends = calloc(workers, sizeof(*read_ends));	// alocação de memória para os descritores de ficheiros para a leitura
  pid_t *pids = calloc(workers, sizeof(*pids));	// alocação de memória para os IDs dos processos filhos	
  if (read_ends == NULL || pids == NULL) {
	free(read_ends);
	free(pids);
	fatal("calloc");
  }

  size_t base = n / workers;
  size_t rem = n % workers;
  size_t cursor = 0;
  for (size_t i = 0; i < workers; ++i) {	// criação dos processos filhos e distribuição dos intervalos do vetor
	int fd[2];
	if (pipe(fd) != 0) {	// criação da pipe e tratamento de erros
	  fatal("pipe");
	}
	size_t len = base + (i < rem ? 1U : 0U);	// tamanho do intervalo do vetor para o processo filho + resto -> +1UL para o primeiro processo filho atual até ao resto ser 0 (ex: 100/3 = 33.33 -> 33 + 1 = 34)
	size_t begin = cursor;
	size_t end = cursor + len;	// intervalo do vetor para o processo filho
	cursor += len;

	pid_t pid = fork();	// criação do processo filho e tratamento de erros
	if (pid < 0) {
	  fatal("fork");
	}
	if (pid == 0) {	// processo filho
	  close(fd[0]);
	  Stats s = calc_range(values, begin, end);	// cálculo dos stats para o intervalo do vetor
	  ssize_t wr = write(fd[1], &s, sizeof(s));
	  close(fd[1]);	// fecho do descritor de ficheiro para a escrita
	  _exit(wr == (ssize_t)sizeof(s) ? 0 : 1);
	}

	close(fd[1]);	// fecho do descritor de ficheiro para a escrita
	read_ends[i] = fd[0];
	pids[i] = pid;	// atribuição do ID do processo filho ao array de IDs dos processos filhos
  }

  Stats out = {0, INT16_MAX, INT16_MIN};	// inicialização dos stats com o valor máximo e mínimo possíveis
  for (size_t i = 0; i < workers; ++i) {
	Stats s = {0, INT16_MAX, INT16_MIN};	// inicialização dos stats com o valor máximo e mínimo possíveis
	ssize_t rd = read(read_ends[i], &s, sizeof(s));
	close(read_ends[i]);	// fecho do descritor de ficheiro para a leitura
	if (rd != (ssize_t)sizeof(s)) {
	  fatal("read child result");
	}
	out.sum += s.sum;	// soma dos stats dos processos filhos
	if (s.min < out.min) out.min = s.min;
	if (s.max > out.max) out.max = s.max;	// atualização dos stats com os stats dos processos filhos
  }
  for (size_t i = 0; i < workers; ++i) {
	int st = 0;	// variável para armazenar o status do processo filho
	if (waitpid(pids[i], &st, 0) < 0) {	// espera pela finalização do processo filho e tratamento de erros
	  fatal("waitpid");
	}
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {	// verifica se o processo filho terminou normalmente
	  fprintf(stderr, "child %ld exited with error\n", (long)pids[i]);
	  exit(EXIT_FAILURE);
	}
  }

  free(read_ends);
  free(pids);
  return out;
}


static void print_stats(const char *label, const Stats *s, double elapsed) { // função para imprimir os stats -> min, max, sum e tempo
  printf("%-12s min=%d max=%d sum=%lld time=%.6fs\n", label, (int)s->min, (int)s->max,
		 s->sum, elapsed);
}

int main(int argc, char **argv) {
  size_t n = 1000000U;
  size_t workers = 4U;
  if (argc >= 2) n = strtoull(argv[1], NULL, 10);
  if (argc >= 3) workers = strtoull(argv[2], NULL, 10);
  if (n == 0U || workers == 0U) {
	fprintf(stderr, "uso: %s <num_elementos> <num_workers>\n", argv[0]);
	return EXIT_FAILURE;
  }

  srand(2026);
  int16_t *values = random_vector(n);
  if (values == NULL) {
	fatal("malloc vector");
  }
  // sequencial
  double t0 = now_seconds();  
  Stats s_seq = calc_seq(values, n);
  double t1 = now_seconds();
  // processos
  Stats s_proc = calc_processes(values, n, workers);
  double t2 = now_seconds();

  // threads
  Stats s_thr = calc_threads(values, n, workers);
  double t3 = now_seconds();

  print_stats("sequencial", &s_seq, t1 - t0);
  print_stats("processos", &s_proc, t2 - t1);
  print_stats("threads", &s_thr, t3 - t2);

  if (s_seq.sum != s_proc.sum || s_seq.sum != s_thr.sum || s_seq.min != s_proc.min ||
	  s_seq.min != s_thr.min || s_seq.max != s_proc.max || s_seq.max != s_thr.max) {
	fprintf(stderr, "ERRO: resultados diferentes entre versoes\n");
	free(values);
	return EXIT_FAILURE;
  }

  free(values);
  return EXIT_SUCCESS;
}
