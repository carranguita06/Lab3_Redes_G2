#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

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

    // Cambia esto si no es localhost
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Dirección inválida");
        return -1;
    }

    // 3. Conectar al broker
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en connect");
        return -1;
    }

    printf("Conectado al broker\n");

    // 4. Identificarse como publisher
    strcpy(buffer, "PUBLISHER");
    send(sock, buffer, strlen(buffer), 0);

    sleep(1); // pequeña pausa

    // 5. Enviar mínimo 10 mensajes
    srand(time(NULL));
    for (int i = 1; i <= 10; i++) {
        memset(buffer, 0, BUFFER_SIZE);

        int id = getpid(); // identificador del publisher
        int tipo = rand() % 3; // 0,1,2

        if (tipo == 0) {
            sprintf(buffer, "Publisher %d -> Gol de Equipo A al minuto %d", id, i * 5);
        } 
        else if (tipo == 1) {
            sprintf(buffer, "Publisher %d -> Cambio: jugador %d entra por jugador %d", id, i, i+10);
        } 
        else {
            sprintf(buffer, "Publisher %d -> Tarjeta amarilla al número %d de Equipo B", id, i);
        }

        send(sock, buffer, strlen(buffer), 0);

        printf("Enviado: %s\n", buffer);

        sleep(2);
    }

    printf("Todos los mensajes enviados\n");

    close(sock);
    return 0;
}