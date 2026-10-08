#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9090
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    // Recibe los temas como argumento: 
    // ./subscriber_udp partido1
    // ./subscriber_udp partido1,partido2
    char *topics = "partido1";
    if (argc >= 2) topics = argv[1];

    int sock;
    struct sockaddr_in broker_addr, from_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(from_addr);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error creando socket");
        return -1;
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &broker_addr.sin_addr);

    // Registrarse con los temas
    snprintf(buffer, BUFFER_SIZE, "SUBSCRIBER:%s", topics);
    sendto(sock, buffer, strlen(buffer), 0,
           (struct sockaddr *)&broker_addr, sizeof(broker_addr));

    printf("Suscrito a temas: [%s]\n", topics);
    printf("Esperando mensajes...\n\n");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        int n = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0,
                         (struct sockaddr *)&from_addr, &addr_len);
        if (n < 0) {
            perror("Error en recvfrom");
            break;
        }

        buffer[n] = '\0';
        printf("Recibido: %s\n", buffer);
    }

    close(sock);
    return 0;
}