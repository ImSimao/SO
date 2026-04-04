#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void fatal_system_error(const char *error_msg) { // Função para tratar erros de sistema
    perror(error_msg);
    exit(EXIT_FAILURE);
}

static void print_status(const char *label, pid_t pid, int status) { // Função para imprimir o status de um processo
    if (WIFEXITED(status)) {    // WIFEXITED é uma macro que verifica se o processo terminou normalmente -> Retorno: verdadeiro (!= 0) se o processo terminou normalmente, falso (== 0) caso contrário.
        printf("%s (pid=%ld) terminou com exit code %d\n",
               label, (long)pid, WEXITSTATUS(status)); // WEXITSTATUS é uma macro que retorna o exit() code do processo, o valor de saída do processo.
    } else if (WIFSIGNALED(status)) { // // WIFSIGNALED é uma macro que indica se o filho foi terminado por um sinal. (ex: SIGKILL, SIGSEGV, SIGINT, etc.)
        printf("%s (pid=%ld) terminou por sinal %d\n",
               label, (long)pid, WTERMSIG(status)); // WTERMSIG é uma macro que indica o numero do sinal que terminou o processo.
    } else { // Se o processo não terminou normalmente nem por um sinal, então terminou com estado desconhecido.
        printf("%s (pid=%ld) terminou com estado desconhecido\n",
               label, (long)pid);
    } 
}

int main(void) { 
    pid_t date_pid = fork(); // Criação do processo filho para executar o comando date
    if (date_pid < 0) {
        fatal_system_error("fork for /bin/date");
    }
    if (date_pid == 0) {
        execl("/bin/date", "date", (char *)NULL);
        fatal_system_error("exec /bin/date");
    }

    pid_t ping_pid = fork(); // Criação do processo filho para executar o comando ping
    if (ping_pid < 0) {
        fatal_system_error("fork for /bin/ping");
    }
    if (ping_pid == 0) {
        execl("/bin/ping", "ping", "-c", "4", "www.google.com", (char *)NULL);
        fatal_system_error("exec /bin/ping");
    }

    int date_status = 0; // Variável para armazenar o status do processo filho para o comando date
    int ping_status = 0; // Variável para armazenar o status do processo filho para o comando ping

    if (waitpid(date_pid, &date_status, 0) < 0) { // Espera pelo processo filho para o comando date
        fatal_system_error("waitpid for /bin/date");
    }
    if (waitpid(ping_pid, &ping_status, 0) < 0) { // Espera pelo processo filho para o comando ping
        fatal_system_error("waitpid for /bin/ping");
    }

    print_status("/bin/date", date_pid, date_status); // Imprime o status do processo filho para o comando date
    print_status("/bin/ping", ping_pid, ping_status); // Imprime o status do processo filho para o comando ping
    return EXIT_SUCCESS;
}
