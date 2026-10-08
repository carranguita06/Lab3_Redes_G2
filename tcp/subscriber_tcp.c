#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // 1. Crear socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error creando socket");
        return -1;
    }

    // 2. Configurar servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Cambia si no es localhost
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Dirección inválida");
        return -1;
    }

    // 3. Conectar al broker
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en connect");
        return -1;
    }

    printf("Conectado al broker como SUBSCRIBER\n");

    // 4. Identificarse
    strcpy(buffer, "SUBSCRIBER");
    send(sock, buffer, strlen(buffer), 0);

    // 5. Recibir mensajes continuamente
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        int valread = recv(sock, buffer, BUFFER_SIZE, 0);

        if (valread <= 0) {
            printf("Conexión cerrada por el broker\n");
            break;
        }

        buffer[valread] = '\0';

        printf("Recibido: %s\n", buffer);
    }

    close(sock);
    return 0;
}