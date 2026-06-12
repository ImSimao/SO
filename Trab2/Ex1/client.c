#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define UNIX_SOCKET_PATH "/tmp/SocketSO"

#define ST_OK 0U
#define ST_INVALID 1U
#define ST_ERR 2U

#define READ_BUF 256

void fatal(const char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

/* Connections */
int connect_unix(const char *path) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fatal("socket AF_UNIX");
	}

	struct sockaddr_un addr; // socket address Unix Domain
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX; // socket family -> (UNIX = Unix Domain)
	if (strlen(path) >= sizeof addr.sun_path) {
		fprintf(stderr, "caminho do socket demasiado longo\n");
		exit(EXIT_FAILURE);
	}
	strncpy(addr.sun_path, path, sizeof addr.sun_path - 1U);

	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		fatal("connect AF_UNIX");
	}
	return fd;
}

int connect_tcp(const char *host, const char *portstr) {
	struct addrinfo
			hints; // socket address TCP -> dá indicação do tipo/família do socket
	struct addrinfo *res = NULL;
	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM; // socket type stream -> dá indicação do tipo
																	 // de socket (STREAM = TCP)
	hints.ai_family = AF_UNSPEC; // socket family -> dá indicação da família do
															 // socket (UNSPEC = IPV4 ou IPV6)

	// getaddrinfo() -> obtém o endereço do socket TCP através das preferências
	// definidas em hints -> res é um ponteiro para a lista de endereços
	int getAddr = getaddrinfo(host, portstr, &hints, &res);
	if (getAddr != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getAddr));
		exit(EXIT_FAILURE);
	}

	int fd = -1;
	for (struct addrinfo *p = res; p != NULL;
			 p = p->ai_next) { // *p cursor de iteração em res -> res é a lista de
												 // endereços //p->ai_next -> pega o próximo endereço da
												 // lista de endereços
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0) {
			continue;
		}
		if (connect(fd, p->ai_addr, p->ai_addrlen) ==
				0) { // connect(fd, p->ai_addr, p->ai_addrlen) -> connecta o socket fd
						 // ao endereço p->ai_addr com o tamanho p->ai_addrlen
			break; // se connect for sucesso, sai do for
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res); // freeaddrinfo(res) -> liberta a memória alocada para res
	if (fd < 0) {
		fatal("connect TCP");
	}
	return fd;
}

/* Read and write unsigned 32-bit integers in big-endian */
ssize_t read_full(int fd, void *buf, size_t n) {
	unsigned char *p = buf;
	size_t got = 0;

	while (got < n) {
		ssize_t r = read(fd, p + got, n - got);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (r == 0) {
			errno = ECONNRESET;
			return -1;
		}
		got += (size_t)r;
	}
	return (ssize_t)n;
}

int write_full(int fd, const void *buf, size_t n) {
	const unsigned char *p = buf;
	size_t sent = 0;

	while (sent < n) {
		ssize_t w = write(fd, p + sent, n - sent);
		if (w < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		sent += (size_t)w;
	}
	return 0;
}

/* Read and write unsigned 8-bit integers */
int read_u8(int fd, uint8_t *msg) {
	return read_full(fd, msg, sizeof *msg) < 0 ? -1 : 0;
}

int write_u8(int fd, uint8_t msg) {
	return write_full(fd, &msg, sizeof msg);
}
/* Read unsigned 32-bit integers in big-endian(usada em protocolos de rede, o
	 byte mais significativo é o primeiro)*/
int read_u32(int fd, uint32_t *out) {
	uint32_t be = 0;
	if (read_full(fd, &be, sizeof be) < 0) {
		return -1;
	}
	*out = ntohl(be); // ntohl(be) -> converte o endereço big-endian para o formato de
										// rede (big-endian)
	return 0;
}

int read_error_body_to_stderr(
		int fd) { // lê o corpo do erro e escreve para o stderr
	uint32_t len = 0;
	if (read_u32(fd, &len) != 0) {
		return -1;
	}
	if (len == 0U) {
		return 0;
	}
	unsigned char *buf = malloc(len);
	if (buf == NULL) {
		return -1;
	}
	if (read_full(fd, buf, len) < 0) {
		free(buf);
		return -1;
	}
	if (write_full(STDERR_FILENO, buf, len) !=
			0) { // escreve o buffer buf para o stderr com o tamanho len
		free(buf);
		return -1;
	}
	free(buf);
	return 0;
}

int print_stdout(int fd, uint32_t len) {
	unsigned char buf[READ_BUF];
	uint32_t left = len;

	while (left > 0U) {
		// chunk -> tamanho do bloco a ler -> se left for maior que sizeof buf,
		// chunk é sizeof buf, caso contrário, chunk é left
		size_t chunk;
		if (left > sizeof buf) {
			chunk = sizeof buf;
		} else {
			chunk = (size_t)left;
		}
		if (read_full(fd, buf, chunk) < 0) {
			return -1;
		}
		if (write_full(STDOUT_FILENO, buf, chunk) != 0) {
			return -1;
		}
		left -= (uint32_t)chunk;
	}
	return 0;
}

int print_stdout_body(int fd) {
	while (1) { // loop infinito -> lê o corpo do ok e escreve para o stdout
		uint32_t len = 0;
		if (read_u32(fd, &len) != 0) {
			return -1;
		}
		if (len == 0U) {
			return 0;
		}
		if (print_stdout(fd, len) != 0) {
			return -1;
		}
	}
}

void usage(const char *prog) {
	fprintf(stderr,
					"uso: %s unix <codigo_servico>\n"
					"     %s tcp <host> <porta> <codigo_servico>\n",
					prog, prog);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	if (argc < 3) {
		usage(argv[0]);
	}

	const char *mode = argv[1];
	int fd = -1;
	unsigned long code = 0;
	if (strcmp(mode, "unix") == 0) {
		if (argc != 3) {
			usage(argv[0]);
		}
		fd = connect_unix(UNIX_SOCKET_PATH);
		code = strtoul(argv[2], NULL,
									 10); // 10 é base decimal, 0 descobre a base automaticamente
	} else if (strcmp(mode, "tcp") == 0) {
		if (argc != 5) {
			usage(argv[0]);
		}
		fd = connect_tcp(argv[2], argv[3]);
		code = strtoul(argv[4], NULL, 10);
	} else {
		usage(argv[0]);
	}

	if (code != 1 && code != 2) { // Verifica se o codigo é valido. poderia
																// alterar os codigos até 255UL = 1 byte
		fprintf(stderr, "codigo de serviço invalido\n");
		return EXIT_FAILURE;
	}

	if (write_u8(fd, (uint8_t)code) !=0) { // Escreve o codigo de serviço no socket
		fatal("write pedido");
	}

	uint8_t st = 0;	// st -> estado do servidor 
	if (read_u8(fd, &st) != 0) { // Le o estado do servidor(resposta do servidor)
		fatal("read estado sv");
	}

	if (st == ST_INVALID) {	// se o estado for invalido, escreve para o stderr
		fprintf(stderr, "serviço invalido\n");
		close(fd);
		return EXIT_FAILURE;
	}
	if (st == ST_ERR) {	// se o estado for erro, lê o corpo do erro e escreve para o stderr
		if (read_error_body_to_stderr(fd) != 0) {
			fatal("read mensagem de erro");
		}
		close(fd);
		return EXIT_FAILURE;
	}
	if (st != ST_OK) {	// se o estado for desconhecido, escreve para o stderr
		fprintf(stderr, "estado desconhecido: %u\n", (unsigned)st);
		close(fd);
		return EXIT_FAILURE;
	}

	if (print_stdout_body(fd) != 0) {	// se o estado for ok (nenhuma condição de erro/desconhecido), lê o corpo do ok e escreve para o stdout
		fatal("read corpo");
	}

	close(fd);
	return EXIT_SUCCESS;
}