#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define UNIX_SOCKET_PATH "/tmp/SocketSO"

#define CHUNK_SIZE 4096

#define SVC_LSCPU 1U
#define SVC_FREE_H 2U

#define ST_OK 0U
#define ST_INVALID 1U
#define ST_ERR 2U

void fatal(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

/**  Funções de read/write **/

/* Le n bytes do socket e escreve no buffer */
ssize_t read_full(int fd, void *buf, size_t n) {
  unsigned char *p = buf;
  size_t got = 0;	// got -> número de bytes lidos || n -> número de bytes a ler

  while (got < n) {
	ssize_t r = read(fd, p + got, n - got);	// read -> le n bytes do socket e escreve no buffer
	if (r < 0) {
	  if (errno == EINTR) {	// EINTR -> erro de interrupção (ex: sinal de interrupção)
		continue;
	  }
	  return -1;
	}
	if (r == 0) {
	  errno = ECONNRESET;	// ECONNRESET -> erro de conexão resetada (ex: socket fechado)
	  return -1;
	}
	got += (size_t)r;
  }
  return (ssize_t)n;
}

/* Escreve n bytes do buffer no socket */
int write_full(int fd, const void *buf, size_t n) {
  const unsigned char *p = buf;
  size_t sent = 0;

  while (sent < n) {	// sent -> número de bytes enviados || n -> número de bytes a enviar
	ssize_t w = write(fd, p + sent, n - sent);	// write -> escreve n bytes do buffer no socket
	if (w < 0) {
	  if (errno == EINTR) {	// EINTR -> erro de interrupção (ex: sinal de interrupção)
		continue;
	  }
	  return -1;
	}
	sent += (size_t)w;
  }
  return 0;
}

/**  Funções de read/write de 1 byte(8bits) (uint8_t)  **/
/* Le 1 byte do socket e escreve no buffer */
int read_u8(int fd, uint8_t *out) {
  return read_full(fd, out, sizeof *out) < 0 ? -1 : 0;
}

/* Escreve 1 byte do buffer no socket */
int write_u8(int fd, uint8_t v) { return write_full(fd, &v, sizeof v); }

/**  Funções de read/write de 4 bytes(32bits) (uint32_t)  **/
/* Escreve 4 bytes do buffer no socket */
int write_u32_be(int fd, uint32_t v) {
  uint32_t be = htonl(v);
  return write_full(fd, &be, sizeof be);
}

/* Escreve o tamanho e os dados do frame no socket */
int write_frame(int fd, const void *data, uint32_t len) {
  if (write_u32_be(fd, len) != 0) {	// write_u32_be -> escreve 4 bytes do buffer no socket
	return -1;
  }
  if (len > 0U && write_full(fd, data, len) != 0) {	// write_full -> escreve n bytes do buffer no socket
	return -1;
  }
  return 0;
}

/* Escreve o estado de erro e a mensagem no socket */
int write_status2_message(int conn_fd, const char *msg) {
  uint32_t len = (uint32_t)strlen(msg);
  if (write_u8(conn_fd, ST_ERR) != 0) {
	return -1;
  }
  return write_frame(conn_fd, msg, len);
}

/* Executa o serviço e escreve a saída no socket */
int run_service_stream(int conn_fd, char *const argv[]) {
  int out_pipe[2];
  int sync_pipe[2];

  if (pipe(out_pipe) != 0) {	// pipe -> cria uma pipe para a saída do serviço
	return write_status2_message(conn_fd, "pipe failed for stdout");
  }
  if (pipe(sync_pipe) != 0) {	// pipe -> cria uma pipe para a sincronização
	close(out_pipe[0]);
	close(out_pipe[1]);
	return write_status2_message(conn_fd, "pipe failed for sync");
  }

  pid_t pid = fork();	// fork -> cria um novo processo filho
  if (pid < 0) {
	close(out_pipe[0]);
	close(out_pipe[1]);
	close(sync_pipe[0]);
	close(sync_pipe[1]);
	return write_status2_message(conn_fd, "fork failed");
  }

  if (pid == 0) { // processo filho
	close(out_pipe[0]);	// fecha o descritor de leitura da pipe de saída
	close(sync_pipe[0]);
	close(conn_fd);	// fecha o socket de conexão

	if (dup2(out_pipe[1], STDOUT_FILENO) < 0) {	// dup2 -> duplica o descritor de escrita da pipe de saída para o stdout
	  perror("dup2 stdout");
	  _exit(126);
	}
	close(out_pipe[1]);	// fecha o descritor de escrita da pipe de saída

	if (fcntl(sync_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {	// fcntl -> define o descritor de fechamento para o processo filho
	  perror("fcntl CLOEXEC sync");
	  _exit(126);	// _exit -> termina o processo filho sem executar o atexit (126 -> comando não encontrado)
	}

	execvp(argv[0], argv);	// execvp -> executa o comando argv[0] com os argumentos argv
	{
	  int saved = errno;	// errno -> variável global que armazena o último erro ocorrido
	  const char *prefix = "execvp: ";
	  char buf[256];
	  size_t msglen =
		  (size_t)snprintf(buf, sizeof buf, "%s%s", prefix, strerror(saved));
	  const char *p = buf;
	  while (msglen > 0U) {
		ssize_t w = write(sync_pipe[1], p, msglen);	// write -> escreve n bytes do buffer no socket
		if (w < 0) {
		  if (errno == EINTR) {	// EINTR -> erro de interrupção (ex: sinal de interrupção)
			continue;
		  }
		  break;
		}
		p += (size_t)w;
		msglen -= (size_t)w;
	  }
	}
	_exit(126);
  }

  close(out_pipe[1]); // fecha o descritor de escrita da pipe de saída
  close(sync_pipe[1]); // fecha o descritor de escrita da pipe de sincronização

  char sync_buf[256];	// buffer para a string de sincronização
  ssize_t sn = read(sync_pipe[0], sync_buf, sizeof sync_buf - 1U);	// read -> le n bytes do socket e escreve no buffer
  close(sync_pipe[0]);
  if (sn < 0) {
	close(out_pipe[0]);
	waitpid(pid, NULL, 0);
	return write_status2_message(conn_fd, "read sync pipe failed");
  }
  if (sn > 0) { // se a string de sincronização for maior que 0, escreve a mensagem de erro no socket
	sync_buf[(size_t)sn] = '\0';	// adiciona o caractere nulo ao final da string sync_buf
	close(out_pipe[0]);
	waitpid(pid, NULL, 0);
	return write_status2_message(conn_fd, sync_buf);	// write_status2_message -> escreve o estado de erro e a mensagem no socket
  }

  if (write_u8(conn_fd, ST_OK) != 0) {	// write_u8 -> escreve 1 byte do buffer no socket
	close(out_pipe[0]);
	waitpid(pid, NULL, 0);
	return -1;
  }

  unsigned char buf[CHUNK_SIZE];
  for (;;) {
	ssize_t n = read(out_pipe[0], buf, sizeof buf);	// read -> le n bytes do socket e escreve no buffer
	if (n < 0) {
	  if (errno == EINTR) {
		continue;
	  }
	  close(out_pipe[0]);
	  waitpid(pid, NULL, 0);
	  return -1;
	}
	if (n == 0) {
	  break;
	}
	if (write_frame(conn_fd, buf, (uint32_t)n) != 0) {	// write_frame -> escreve o tamanho e os dados do frame no socket
	  close(out_pipe[0]);
	  waitpid(pid, NULL, 0);
	  return -1;
	}
  }
  close(out_pipe[0]);

  if (waitpid(pid, NULL, 0) < 0) {
	return write_status2_message(conn_fd, "waitpid failed");
  }

  if (write_u32_be(conn_fd, 0U) != 0) {
	return -1;
  }
  return 0;
}

/* tratamento da sessão do cliente */
void handle_client_session(int conn_fd) {
  uint8_t svc = 0;	// svc -> código de serviço
  if (read_u8(conn_fd, &svc) != 0) {
	return;
  }

  if (svc != SVC_LSCPU && svc != SVC_FREE_H) {
	(void)write_u8(conn_fd, ST_INVALID); // escreve o estado de erro no socket
	return;
  }

  char *argv_lscpu[] = {"lscpu", NULL};	// argv_lscpu -> array de strings com o comando lscpu
  char *argv_free[] = {"free", "-h", NULL};	// argv_free -> array de strings com o comando free -h
  char *const *av =	// av -> ponteiro para o array de strings com o comando lscpu ou free -h
	  (svc == SVC_LSCPU) ? (char *const *)argv_lscpu : (char *const *)argv_free;

  (void)run_service_stream(conn_fd, (char *const *)av); // executa o serviço e escreve a saída no socket
}

/**  Funções de listen/accept  **/
/* cria e inicia o socket Unix */
int listen_unix_stream(const char *path) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
	fatal("socket AF_UNIX");
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  if (strlen(path) >= sizeof addr.sun_path) {	// strlen(path) -> tamanho da string path || sizeof addr.sun_path -> tamanho do array addr.sun_path
	fprintf(stderr, "socket path too long\n");
	exit(EXIT_FAILURE);
  }
  strncpy(addr.sun_path, path, sizeof addr.sun_path - 1U);

  unlink(path); // remove o socket existente se existir (evita erro de se o servidor parou/chashou sem fechar o socket (EADDRINUSE))
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) { // associa o socket fd ao endereço addr com o tamanho sizeof addr
	fatal("bind AF_UNIX");
  }
  if (listen(fd, 16) != 0) { // listen(fd, 16) -> coloca o socket fd em modo de escuta para 16 conexões (backlog)
	fatal("listen AF_UNIX");
  }
  return fd;
}

/* cria e inicia o socket TCP */
int listen_tcp_stream(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
	fatal("socket AF_INET");
  }

  int one = 1;
  // setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) -> permite que o socket seja reutilizado imediatamente após ser fechado (evita erro de se o servidor parou/chashou sem fechar o socket (EADDRINUSE))
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0) {
	fatal("setsockopt SO_REUSEADDR");
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
	fatal("bind AF_INET");
  }
  if (listen(fd, 16) != 0) {
	fatal("listen AF_INET");
  }
  return fd;
}

/* Termina os processos filhos que acabaram */
void reap_children(void) { 
  int st = 0;
  while (waitpid(-1, &st, WNOHANG) > 0) {
  }
}

/* Loop principal do servidor para um socket */
void server_loop_single(int listen_fd) {
  for (;;) {
	reap_children();
	int conn = accept(listen_fd, NULL, NULL);
	if (conn < 0) {
	  if (errno == EINTR) {
		continue;
	  }
	  fatal("accept");
	}

	pid_t pid = fork(); // cria um novo processo filho
	if (pid < 0) {
	  perror("fork");
	  close(conn);
	  continue;
	}
	if (pid == 0) {
	  close(listen_fd); // fecha o socket de escuta
	  handle_client_session(conn);
	  close(conn);
	  _exit(0); // termina o processo filho (_exit -> termina o processo filho sem executar o atexit) (atexit -> funções que são executadas quando o processo termina)
	}
	close(conn);
  }
}

/* Loop principal do servidor para dois sockets */
void serve_loop_dual(int unix_fd, int tcp_fd) {
  struct pollfd pfds[2];
  pfds[0].fd = unix_fd;
  pfds[0].events = POLLIN;
  pfds[1].fd = tcp_fd;
  pfds[1].events = POLLIN;

  for (;;) {	//loop infinito para escutar os sockets
	reap_children();
	int pr = poll(pfds, 2, -1);
	if (pr < 0) {
	  if (errno == EINTR) {
		continue;
	  }
	  fatal("poll");
	}

	for (int i = 0; i < 2; ++i) {
	  if ((pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
		fatal("poll listen error");
	  }
	}

	if ((pfds[0].revents & POLLIN) != 0) {
	  int conn = accept(unix_fd, NULL, NULL);
	  if (conn < 0) {
		if (errno == EINTR) {
		  continue;
		}
		fatal("accept unix");
	  }
	  pid_t pid = fork();
	  if (pid < 0) {
		perror("fork");
		close(conn);
	  } else if (pid == 0) {
		close(unix_fd);
		close(tcp_fd);
		handle_client_session(conn);
		close(conn);
		_exit(0);
	  }
	  close(conn);
	}

	if ((pfds[1].revents & POLLIN) != 0) {
	  int conn = accept(tcp_fd, NULL, NULL);
	  if (conn < 0) {
		if (errno == EINTR) {
		  continue;
		}
		fatal("accept tcp");
	  }
	  pid_t pid = fork();
	  if (pid < 0) {
		perror("fork");
		close(conn);
	  } else if (pid == 0) {
		close(unix_fd);
		close(tcp_fd);
		handle_client_session(conn);
		close(conn);
		_exit(0);
	  }
	  close(conn);
	}
  }
}

void usage(const char *prog) {
  fprintf(stderr,
		  "uso: %s unix\n"
		  "     %s tcp <porta>\n"
		  "     %s both <porta>\n",
		  prog, prog, prog);
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  if (argc < 2) {
	usage(argv[0]);
  }

  const char *mode = argv[1];

  if (strcmp(mode, "unix") == 0) {

	int fd = listen_unix_stream(UNIX_SOCKET_PATH);
	server_loop_single(fd); // loop principal do servidor para um socket com fd para path Unix

  } else if (strcmp(mode, "tcp") == 0) {
	if (argc != 3) {
	  usage(argv[0]);
	}

	unsigned long p = strtoul(argv[2], NULL, 10);
	if (p == 0UL || p > 65535UL) {
	  fprintf(stderr, "porta invalida\n");
	  return EXIT_FAILURE;
	}
	int fd = listen_tcp_stream((uint16_t)p);
	server_loop_single(fd); // loop principal do servidor para um socket com fd para porta TCP

  } else if (strcmp(mode, "both") == 0) {

	if (argc != 3) {
	  usage(argv[0]);
	}
	unsigned long p = strtoul(argv[2], NULL, 10);
	if (p == 0UL || p > 65535UL) {
	  fprintf(stderr, "porta invalida\n");
	  return EXIT_FAILURE;
	}
	int ufd = listen_unix_stream(UNIX_SOCKET_PATH); // cria e inicia o socket Unix
	int tfd = listen_tcp_stream((uint16_t)p); // cria e inicia o socket TCP
	serve_loop_dual(ufd, tfd); // loop principal do servidor para dois sockets

  } else {
	usage(argv[0]);
  }

  return EXIT_SUCCESS;
}
