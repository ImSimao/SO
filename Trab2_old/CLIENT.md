# Explicacao do `client2.c` (ordem de execucao e read/write)

Este documento explica o fluxo do cliente, em que ordem as funcoes correm, e porque as funcoes de `read` e `write` existem.

## 1) Fluxo geral do programa

No `main`, o cliente faz este caminho:

1. Valida os argumentos.
2. Escolhe o modo:
   - `unix` -> `connect_unix(UNIX_SOCKET_PATH)`
   - `tcp` -> `connect_tcp(host, port)`
3. Valida o codigo de servico.
4. Envia 1 byte para o servidor com `write_u8(fd, code)`.
5. Le 1 byte de estado com `read_u8(fd, &st)`.
6. Decide pelo estado:
   - `ST_INVALID` -> termina com erro.
   - `ST_ERR` -> `read_error_body_to_stderr(fd)`.
   - `ST_OK` -> `print_stdout_body(fd)`.
7. Fecha o socket e termina.

## 2) Ligacao ao servidor

### `connect_unix(const char *path)`

- Cria socket `AF_UNIX`/`SOCK_STREAM`.
- Preenche `struct sockaddr_un`:
  - `sun_family = AF_UNIX`
  - `sun_path = path`
- Faz `connect(...)`.

Isto e para comunicacao local por ficheiro de socket (ex.: `/tmp/SocketSO`).

### `connect_tcp(const char *host, const char *portstr)`

- Preenche `hints` com:
  - `ai_socktype = SOCK_STREAM` (TCP)
  - `ai_family = AF_UNSPEC` (IPv4 ou IPv6)
- Chama `getaddrinfo(host, portstr, &hints, &res)`.
- Percorre a lista com `for (struct addrinfo *p = res; ...)`.
- Para cada `p`: cria socket e tenta `connect`.
- Se ligar, sai do ciclo.
- Faz `freeaddrinfo(res)` no fim.

Importante:
- `res` e a cabeca da lista devolvida por `getaddrinfo`.
- `p` e so o cursor para iterar essa lista.

## 3) Porque existem `read_full` e `write_full`

Em sockets, `read()` e `write()` podem processar menos bytes do que o pedido.

Exemplo: pedes 100 bytes e `read()` devolve 20. E normal.

Por isso:

### `read_full(int fd, void *buf, size_t n)`
- Repete `read()` ate obter exatamente `n` bytes.
- Trata `EINTR`.
- Se a ligacao fechar antes do esperado (`r == 0`), retorna erro.

### `write_full(int fd, const void *buf, size_t n)`
- Repete `write()` ate enviar exatamente `n` bytes.
- Trata `EINTR`.

Sem estas funcoes, o protocolo podia ficar dessincronizado (cabecalhos incompletos, tamanhos errados, etc.).

## 4) Funcoes auxiliares de protocolo

### `write_u8(int fd, uint8_t msg)` e `read_u8(int fd, uint8_t *msg)`

- Enviam/leem exatamente 1 byte.
- Usadas para:
  - pedido do cliente (codigo de servico)
  - estado devolvido pelo servidor

### `read_u32(int fd, uint32_t *out)`

- Le 4 bytes com `read_full`.
- Converte de big-endian para a ordem da maquina com `ntohl`.

Isto garante que os valores recebidos sao corretos em qualquer arquitetura.

## 5) Caminho de erro do servidor

### `read_error_body_to_stderr(int fd)`

1. Le `len` (u32) com `read_u32`.
2. Se `len == 0`, nao ha mensagem.
3. Aloca `len` bytes.
4. Le exatamente `len` bytes do socket.
5. Escreve esses bytes para `STDERR_FILENO`.

Nota: aqui o cliente **nao envia** nada ao servidor.  
O `write_full(..., STDERR_FILENO, ...)` escreve no terminal local (stream de erro).

## 6) Caminho de sucesso (ST_OK)

### `print_stdout_body(int fd)`

Faz loop:
1. Le `len` com `read_u32`.
2. Se `len == 0`, fim da resposta.
3. Caso contrario, chama `print_stdout(fd, len)`.

### `print_stdout(int fd, uint32_t len)`

- Copia exatamente `len` bytes do socket para `stdout` em blocos (`READ_BUF`).
- Usa:
  - `read_full(fd, buf, chunk)`
  - `write_full(STDOUT_FILENO, buf, chunk)`

Porque em blocos:
- `len` pode ser grande.
- Evita alocar memoria gigante.
- Mantem copia segura e simples.

## 7) Simulacao de bytes no socket

### Pedido do cliente

Se o codigo de servico for `2`:
- Cliente -> Servidor: `02` (1 byte)

### Resposta de sucesso

Servidor manda:
- estado `ST_OK`: `00`
- bloco com 4 bytes (`"Ola\n"`):
  - tamanho: `00 00 00 04`
  - dados: `4F 6C 61 0A`
- fim: `00 00 00 00`

Stream completa:

`00 | 00 00 00 04 | 4F 6C 61 0A | 00 00 00 00`

### Resposta de erro

Servidor manda:
- estado `ST_ERR`: `02`
- tamanho da mensagem (ex.: 13): `00 00 00 0D`
- dados da mensagem (13 bytes)

O cliente le e imprime no `stderr`.

## 8) Resumo rapido

- `main` coordena o fluxo.
- `connect_unix`/`connect_tcp` so ligam ao servidor.
- `write_u8` envia pedido.
- `read_u8` le estado.
- `read_error_body_to_stderr` trata erro.
- `print_stdout_body` e `print_stdout` tratam sucesso.
- `read_full`/`write_full` sao a base para garantir bytes exatos no protocolo.
