#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void fatal_system_error(const char *error_msg) { // Função para tratar erros de sistema
	perror(error_msg);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Uso: %s <ficheiro>\n", argv[0]);
		return EXIT_FAILURE;
	}
	pid_t ping_pid = fork(); // Criação do processo filho para executar o comando ping
	if (ping_pid < 0) {
		fatal_system_error("fork for /bin/ping");
	}
	if (ping_pid == 0) {
		execl("/bin/cat", "cat", argv[1], (char *)NULL);
		fatal_system_error("exec /bin/ping");
	}
	int ping_status = 0; // Variável para armazenar o status do processo filho para o comando ping
	
	if (waitpid(ping_pid, &ping_status, 0) < 0) { // Espera pelo processo filho para o comando ping
		fatal_system_error("waitpid for /bin/ping");
	}


	int pipe_fd[2]; // Descritor de ficheiro para a pipe
	if (pipe(pipe_fd) < 0) {
		fatal_system_error("pipe");
	}

	pid_t pid = fork(); // Criação do processo filho e tratamento de erros
	if (pid < 0) {
		fatal_system_error("fork");
	}

	if (pid == 0) {
		if (close(pipe_fd[0]) < 0) { // Fecho do descritor de ficheiro para a leitura
			fatal_system_error("close read end in child");
		}

		if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) { // Redirecionamento do stdout para a pipe
			fatal_system_error("dup2 stdout in child");
		}
		if (close(pipe_fd[1]) < 0) { // Fecho do descritor de ficheiro para a escrita
			fatal_system_error("close write end in child");
		}

		execlp("wc", "wc", "-w", argv[1], (char *)NULL); // Execução do comando wc -w
		fatal_system_error("exec wc -w");
	}

	if (close(pipe_fd[1]) < 0) { // Fecho do descritor de ficheiro para a escrita
		fatal_system_error("close write end in parent");
	}

	char buffer[256]; // Buffer para armazenar o resultado do comando wc -w
	ssize_t total = 0;
	while (total < (ssize_t)(sizeof(buffer) - 1)) {
		ssize_t n = read(pipe_fd[0], buffer + total, sizeof(buffer) - 1 - (size_t)total);
		if (n < 0) { // Leitura do resultado do comando wc -w
			if (errno == EINTR) { // Se o sinal for interrompido, continuar a ler (EINTR -> Interrupted system call)
				continue;
			}
			fatal_system_error("read from pipe"); // Tratamento de erros
		}
		if (n == 0) {
			break; // Se não houver mais dados para ler, sair do loop
		}
		total += n; // Incremento do total
	}
	buffer[total] = '\0';

	if (close(pipe_fd[0]) < 0) { // Fecho do descritor de ficheiro para a leitura
		fatal_system_error("close read end in parent");
	}

	int status = 0; // Variável para armazenar o status do processo filho
	if (waitpid(pid, &status, 0) < 0) {
		fatal_system_error("waitpid");
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { // Verificação se o processo filho terminou normalmente (WIFEXITED -> Verifica se o processo terminou normalmente, WEXITSTATUS -> Retorna o exit() code do processo)
		fprintf(stderr, "wc terminou com erro\n");
		return EXIT_FAILURE;
	}

	char *p = buffer;
	while (*p != '\0' && isspace((unsigned char)*p)) {
		++p;
	}
	if (*p == '\0') {
		fprintf(stderr, "Nao foi possivel obter contagem de palavras\n");
		return EXIT_FAILURE;
	}

	char *endptr = NULL;
	long words = strtol(p, &endptr, 10);
	if (endptr == p) {
		fprintf(stderr, "Saida inesperada do wc: %s\n", buffer);
		return EXIT_FAILURE;
	}

	printf("%ld\n", words);
	return EXIT_SUCCESS;
}
