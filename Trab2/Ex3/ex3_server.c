#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define ST_OK 0U
#define ST_INVALID 1U
#define ST_ERR 2U

typedef struct {  
  uint16_t min;
  uint16_t max;
  uint64_t sum;
} Stats;

typedef struct {  
  const uint16_t *values;
  size_t start;
  size_t end;
  Stats out;
} WorkerTask;

static ssize_t read_full(int fd, void *buf, size_t n) { // le n bytes do socket e escreve no buffer
  unsigned char *p = buf;
  size_t got = 0;
  while (got < n) {
    ssize_t r = read(fd, p + got, n - got);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (r == 0) return 0;
    got += (size_t)r;
  }
  return (ssize_t)got;
}

static int write_full(int fd, const void *buf, size_t n) { // escreve n bytes do buffer no socket
  const unsigned char *p = buf;
  size_t sent = 0;
  while (sent < n) {
    ssize_t w = write(fd, p + sent, n - sent);
    if (w < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    sent += (size_t)w;
  }
  return 0;
}

// converte um valor de 64 bits para o formato big endian
static uint64_t htonll(uint64_t v) { 
  uint32_t hi = htonl((uint32_t)(v >> 32));
  uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFFU));
  return ((uint64_t)lo << 32) | hi; 
}

static int write_u8(int fd, uint8_t v) { return write_full(fd, &v, 1U); } // escreve 1 byte do buffer no socket

static int read_u32_be(int fd, uint32_t *out) { // le 4 bytes do socket e escreve no buffer
  uint32_t be = 0;
  ssize_t r = read_full(fd, &be, sizeof(be));
  if (r <= 0) return -1;
  *out = ntohl(be);
  return 0;
}

static int write_frame(int fd, const void *data, uint32_t len) { // escreve o tamanho e os dados do frame no socket
  uint32_t be = htonl(len);
  if (write_full(fd, &be, sizeof(be)) != 0) return -1;
  if (len > 0 && write_full(fd, data, len) != 0) return -1;
  return 0;
}

static int send_status_msg(int fd, uint8_t st, const char *msg) { // envia uma mensagem de status para o cliente
  if (write_u8(fd, st) != 0) return -1;
  return write_frame(fd, msg, (uint32_t)strlen(msg));
}

static void *worker_fn(void *arg) { // função para o thread worker -> calcula os stats para um intervalo do vetor
  WorkerTask *t = (WorkerTask *)arg;
  t->out.min = UINT16_MAX;
  t->out.max = 0; 
  t->out.sum = 0;
  for (size_t i = t->start; i < t->end; ++i) {
    uint16_t v = t->values[i];
    if (v < t->out.min) t->out.min = v;
    if (v > t->out.max) t->out.max = v;
    t->out.sum += v;
  }
  return NULL;
}

static int compute_stats_parallel(const uint16_t *values, size_t n, Stats *out) { // calcula os stats para um vetor com threads
  long cpus = sysconf(_SC_NPROCESSORS_ONLN); // sysconf -> obtém o número de processadores disponíveis
  size_t workers = (cpus > 0) ? (size_t)cpus : 4U;
  if (workers > n) workers = n;
  if (workers == 0) return -1;

  pthread_t *tids = calloc(workers, sizeof(*tids));
  WorkerTask *tasks = calloc(workers, sizeof(*tasks));
  if (tids == NULL || tasks == NULL) {
    free(tids);
    free(tasks);
    return -1;
  }

  size_t base = n / workers;
  size_t rem = n % workers;
  size_t cursor = 0;
  for (size_t i = 0; i < workers; ++i) {
    size_t len = base + (i < rem ? 1U : 0U);
    tasks[i].values = values;
    tasks[i].start = cursor;
    tasks[i].end = cursor + len;
    cursor += len;
    if (pthread_create(&tids[i], NULL, worker_fn, &tasks[i]) != 0) {
      free(tids);
      free(tasks);
      return -1;
    }
  }

  out->min = UINT16_MAX;
  out->max = 0;
  out->sum = 0;
  for (size_t i = 0; i < workers; ++i) {
    if (pthread_join(tids[i], NULL) != 0) {
      free(tids);
      free(tasks);
      return -1;
    }
    if (tasks[i].out.min < out->min) out->min = tasks[i].out.min;
    if (tasks[i].out.max > out->max) out->max = tasks[i].out.max;
    out->sum += tasks[i].out.sum;
  }

  free(tids);
  free(tasks);
  return 0;
}

typedef struct { // contexto do cliente
  int conn_fd; // descritor de conexão
} ClientCtx;

static void *client_thread(void *arg) { // função para o thread cliente -> lida com o cliente e processa o vetor
  ClientCtx *ctx = (ClientCtx *)arg;
  int fd = ctx->conn_fd;
  free(ctx);

  uint16_t *values = NULL; // vetor de valores
  size_t count = 0; // número de elementos no vetor
  size_t cap = 0; // capacidade do vetor
  int parse_error = 0; // erro de parsing
  char errbuf[128] = "pedido invalido";

  while (1) { // loop infinito -> lê o vetor do cliente e processa os stats
    uint32_t len = 0; // tamanho do vetor
    if (read_u32_be(fd, &len) != 0) {
      parse_error = 1; // erro de parsing
      snprintf(errbuf, sizeof(errbuf), "pedido truncado"); // snprintf -> escreve a mensagem de erro "pedido truncado" no buffer errbuf
      break;
    }
    if (len == 0U) { // se o tamanho for 0, termina o loop
      break;
    }
    if ((len % sizeof(uint16_t)) != 0U) {
      parse_error = 1; // erro de parsing
      snprintf(errbuf, sizeof(errbuf), "bloco com dimensao invalida");
      break;
    }
    size_t elems = (size_t)(len / sizeof(uint16_t)); // número de elementos no vetor
    if (count > SIZE_MAX - elems) { // se o número de elementos for maior que o tamanho máximo do vetor, erro de parsing
      parse_error = 1;
      snprintf(errbuf, sizeof(errbuf), "vetor demasiado grande");
      break;
    }
    if (count + elems > cap) { // se o número de elementos for maior que a capacidade do vetor, aumenta a capacidade do vetor
      size_t new_cap = (cap == 0U) ? 1024U : cap;
      while (new_cap < count + elems) { // loop infinito -> aumenta a capacidade do vetor
        if (new_cap > SIZE_MAX / 2U) { // se a capacidade do vetor for maior que o tamanho máximo do vetor, erro de parsing
          parse_error = 1;
          snprintf(errbuf, sizeof(errbuf), "vetor demasiado grande");
          break;
        }
        new_cap *= 2U; // aumenta a capacidade do vetor em 2x
      }
      if (parse_error) break;
      uint16_t *tmp = realloc(values, new_cap * sizeof(*values)); // realoca o vetor com a nova capacidade
      if (tmp == NULL) {
        send_status_msg(fd, ST_ERR, "falha de memoria"); // envia uma mensagem de erro para o cliente
        free(values);
        close(fd);
        return NULL;
      }
      values = tmp; // atualiza o vetor com o novo vetor
      cap = new_cap;
    }
    for (size_t i = 0; i < elems; ++i) { // loop para ler os elementos do vetor
      uint16_t be = 0;
      ssize_t rr = read_full(fd, &be, sizeof(be)); // le 2 bytes do socket e escreve no buffer
      if (rr <= 0) {
        parse_error = 1; // erro de parsing
        snprintf(errbuf, sizeof(errbuf), "pedido truncado");
        break;
      }
      values[count++] = ntohs(be); // converte o valor para uint16_t e adiciona ao vetor
    }
    if (parse_error) break;
  }

  if (!parse_error && count == 0U) { // se o vetor estiver vazio, erro de parsing
    parse_error = 1;
    snprintf(errbuf, sizeof(errbuf), "vetor vazio"); 
  }

  if (parse_error) {
    (void)send_status_msg(fd, ST_INVALID, errbuf);  
    free(values);
    close(fd);
    return NULL;
  }

  Stats s; // stats para o vetor
  if (compute_stats_parallel(values, count, &s) != 0) { // calcula os stats para o vetor com threads
    (void)send_status_msg(fd, ST_ERR, "erro no processamento paralelo");
    free(values);
    close(fd);
    return NULL;
  }

  if (write_u8(fd, ST_OK) != 0) { // escreve o estado de sucesso para o cliente
    free(values);
    close(fd);
    return NULL;
  }

  uint16_t min_be = htons(s.min); // converte o valor minimo para uint16_t e escreve no buffer
  uint16_t max_be = htons(s.max); // converte o valor maximo para uint16_t e escreve no buffer
  uint64_t sum_be = htonll(s.sum); // converte o valor soma para uint64_t e escreve no buffer
  if (write_full(fd, &min_be, sizeof(min_be)) != 0 || // escreve o valor minimo, maximo e soma no socket
      write_full(fd, &max_be, sizeof(max_be)) != 0 ||
      write_full(fd, &sum_be, sizeof(sum_be)) != 0) {
    free(values); 
    close(fd);
    return NULL;
  }

  free(values);
  close(fd);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "uso: %s <porto>\n", argv[0]);
    return EXIT_FAILURE;
  }

  unsigned long port = strtoul(argv[1], NULL, 10);
  if (port == 0UL || port > 65535UL) {
    fprintf(stderr, "porto invalido\n");
    return EXIT_FAILURE;
  }

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return EXIT_FAILURE;
  }
  int one = 1;
  // setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) -> permite que o socket seja reutilizado imediatamente após ser fechado (evita erro de se o servidor parou/chashou sem fechar o socket (EADDRINUSE))
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
    perror("setsockopt");
    close(listen_fd);
    return EXIT_FAILURE;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // htonl -> converte um valor de 32 bits para o formato big endian; INADDR_ANY -> endereço IPANY (0.0.0.0)
  addr.sin_port = htons((uint16_t)port);
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    perror("bind");
    close(listen_fd);
    return EXIT_FAILURE;
  }
  if (listen(listen_fd, 64) != 0) {
    perror("listen");
    close(listen_fd);
    return EXIT_FAILURE;
  }

  for (;;) {
    int conn = accept(listen_fd, NULL, NULL);
    if (conn < 0) {
      if (errno == EINTR) continue;
      perror("accept");
      continue;
    }

    ClientCtx *ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
      close(conn);
      continue;
    }
    ctx->conn_fd = conn;

    pthread_t tid;
    if (pthread_create(&tid, NULL, client_thread, ctx) != 0) {
      close(conn);
      free(ctx);
      continue;
    }
    (void)pthread_detach(tid);
  }
}
