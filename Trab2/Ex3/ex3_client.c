#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
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

typedef struct { // tarefa do cliente
  const char *host;
  const char *port;
  size_t n;
  size_t idx;
} ClientTask;

static ssize_t read_full(int fd, void *buf, size_t n) { // le n bytes do socket e escreve no buffer
  unsigned char *p = buf;
  size_t got = 0;
  while (got < n) {
    ssize_t r = read(fd, p + got, n - got); // read -> le n bytes do socket e escreve no buffer
    if (r < 0) {
      if (errno == EINTR) continue; // se o erro for EINTR, continua o loop
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
    ssize_t w = write(fd, p + sent, n - sent); // write -> escreve n bytes do buffer no socket
    if (w < 0) {
      if (errno == EINTR) continue; // se o erro for EINTR, continua o loop
      return -1;
    }
    sent += (size_t)w;
  }
  return 0;
}

static uint64_t ntohll(uint64_t v) { // converte um valor de 64 bits para o formato little endian
  uint32_t lo = ntohl((uint32_t)(v >> 32)); // ntohl -> converte um valor de 32 bits para o formato little endian || >> 32 -> desloca 32 bits para a direita
  uint32_t hi = ntohl((uint32_t)(v & 0xFFFFFFFFU)); // ntohl -> converte um valor de 32 bits para o formato little endian || & 0xFFFFFFFFU -> máscara para os 32 bits menos significativos
  return ((uint64_t)hi << 32) | lo; // << 32 -> desloca 32 bits para a esquerda
}

static int connect_tcp(const char *host, const char *port) { // conecta ao servidor TCP
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC; // AF_UNSPEC -> IPV4 ou IPV6
  hints.ai_socktype = SOCK_STREAM;
  int rc = getaddrinfo(host, port, &hints, &res); // getaddrinfo -> obtém o endereço do socket TCP através das preferências definidas em hints
  if (rc != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc)); // gai -> getaddrinfo error
    return -1; // erro ao obter o endereço do socket TCP
  }
  int fd = -1;
  for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol); // socket -> cria um novo socket
    if (fd < 0) continue;
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break; // connect -> connecta o socket fd ao endereço p->ai_addr com o tamanho p->ai_addrlen
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res); // freeaddrinfo -> liberta a memória alocada para res
  return fd;
}

static int write_frame(int fd, const void *data, uint32_t len) { // escreve o tamanho e os dados do frame no socket
  uint32_t be = htonl(len); // htonl -> converte um valor de 32 bits para o formato big endian
  if (write_full(fd, &be, sizeof(be)) != 0) return -1;
  if (len > 0 && write_full(fd, data, len) != 0) return -1; // write_full -> escreve n bytes do buffer no socket
  return 0;
}

static int read_error_message(int fd, char *buf, size_t cap) { // lê a mensagem de erro do socket

  uint32_t len_be = 0;
  if (read_full(fd, &len_be, sizeof(len_be)) <= 0) return -1; // read_full -> le n bytes do socket e escreve no buffer
  uint32_t len = ntohl(len_be); // ntohl -> converte um valor de 32 bits para o formato little endian
  if (len + 1U > cap) len = (uint32_t)(cap - 1U); // se o tamanho da mensagem for maior que a capacidade do buffer, ajusta o tamanho da mensagem
  if (read_full(fd, buf, len) <= 0) return -1; // read_full -> le n bytes do socket e escreve no buffer
  buf[len] = '\0'; // adiciona o caractere nulo ao final da mensagem
  return 0;
}

static int make_request(const char *host, const char *port, size_t n, size_t idx) { // faz uma requisição ao servidor
  int fd = connect_tcp(host, port);
  if (fd < 0) {
    fprintf(stderr, "[%zu] erro a ligar ao servidor\n", idx);
    return -1;
  }

  uint16_t *values = malloc(n * sizeof(*values)); // alocação de memória para o vetor de valores
  if (values == NULL) {
    close(fd);
    return -1;
  }
  for (size_t i = 0; i < n; ++i) { // loop para gerar os valores aleatórios
    values[i] = (uint16_t)(rand() & 0xFFFFU);
  }

  uint16_t *be = malloc(n * sizeof(*be)); // alocação de memória para o vetor de valores em rede (big endian)
  if (be == NULL) {
    free(values);
    close(fd);
    return -1;
  }
  for (size_t i = 0; i < n; ++i) { // loop para converter os valores para big endian
    be[i] = htons(values[i]);
  }

  if (write_frame(fd, be, (uint32_t)(n * sizeof(uint16_t))) != 0 || write_frame(fd, NULL, 0U) != 0) { // write_frame -> escreve o tamanho e os dados do frame no socket
    fprintf(stderr, "[%zu] erro ao enviar pedido\n", idx);
    free(be);
    free(values);
    close(fd);
    return -1;
  }

  uint8_t st = 0; // estado da resposta
  if (read_full(fd, &st, 1U) <= 0) { // read_full -> le 1 byte do socket e escreve no buffer
    fprintf(stderr, "[%zu] resposta truncada\n", idx);
    free(be);
    free(values);
    close(fd);
    return -1;
  }

  if (st == ST_OK) { // se o estado for OK, lê os valores minimo, maximo e soma
    uint16_t min_be = 0, max_be = 0;
    uint64_t sum_be = 0;
    if (read_full(fd, &min_be, sizeof(min_be)) <= 0 || read_full(fd, &max_be, sizeof(max_be)) <= 0 ||
        read_full(fd, &sum_be, sizeof(sum_be)) <= 0) { // read_full -> le n bytes do socket e escreve no buffer
      fprintf(stderr, "[%zu] resposta ok truncada\n", idx);
      free(be);
      free(values);
      close(fd);
      return -1;
    }
    uint16_t min = ntohs(min_be); // ntohs -> converte um valor de 16 bits para o formato little endian
    uint16_t max = ntohs(max_be); // ntohs -> converte um valor de 16 bits para o formato little endian
    uint64_t sum = ntohll(sum_be); // ntohll -> converte um valor de 64 bits para o formato little endian
    printf("[%zu] ok: min=%u max=%u sum=%llu\n", idx, (unsigned)min, (unsigned)max,
           (unsigned long long)sum);
  } else {
    char msg[256]; // buffer para a mensagem de erro
    if (read_error_message(fd, msg, sizeof(msg)) != 0) {
      snprintf(msg, sizeof(msg), "(mensagem indisponivel)"); // snprintf -> escreve a mensagem de erro "mensagem indisponivel" no buffer msg
    }
    fprintf(stderr, "[%zu] erro status=%u: %s\n", idx, (unsigned)st, msg);
    free(be);
    free(values);
    close(fd);
    return -1;
  }

  free(be);
  free(values);
  close(fd);
  return 0;
}

static void *task_fn(void *arg) { // função para o thread cliente -> faz uma requisição ao servidor
  ClientTask *t = (ClientTask *)arg;
  (void)make_request(t->host, t->port, t->n, t->idx);
  return NULL;
}

int main(int argc, char **argv) { 
  if (argc < 4 || argc > 5) {
    fprintf(stderr, "uso: %s <ip|host> <porto> <num_elementos> [num_ligacoes]\n", argv[0]);
    return EXIT_FAILURE;
  }

  size_t n = strtoull(argv[3], NULL, 10); // número de elementos no vetor
  size_t links = (argc == 5) ? strtoull(argv[4], NULL, 10) : 1U; // número de ligações -> se o número de argumentos for 5, o número de ligações é o quarto argumento, caso contrário, é 1
  if (n == 0U || links == 0U) { // se o número de elementos ou o número de ligações for 0, erro de argumentos
    fprintf(stderr, "argumentos invalidos\n");
    return EXIT_FAILURE;
  }

  srand(2026); // srand -> inicializa o gerador de números aleatórios

  if (links == 1U) { // se o número de ligações for 1, executa uma única requisição
    return make_request(argv[1], argv[2], n, 0U) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  pthread_t *ids = calloc(links, sizeof(*ids)); // alocação de memória para os IDs das threads
  ClientTask *tasks = calloc(links, sizeof(*tasks)); // alocação de memória para as tasks das threads
  if (ids == NULL || tasks == NULL) { 
    free(ids);
    free(tasks);
    return EXIT_FAILURE;
  }

  for (size_t i = 0; i < links; ++i) { // loop para criar os threads
    tasks[i].host = argv[1];
    tasks[i].port = argv[2];
    tasks[i].n = n;
    tasks[i].idx = i;
    if (pthread_create(&ids[i], NULL, task_fn, &tasks[i]) != 0) { // cria o thread
      fprintf(stderr, "pthread_create falhou\n");
      free(ids);
      free(tasks);
      return EXIT_FAILURE;
    }
  }
  for (size_t i = 0; i < links; ++i) { // loop para esperar os threads terminarem
    (void)pthread_join(ids[i], NULL);
  }

  free(ids);
  free(tasks);
  return EXIT_SUCCESS;
}
