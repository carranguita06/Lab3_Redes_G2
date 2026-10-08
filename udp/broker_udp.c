#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9090
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024
#define MAX_TOPICS 5
#define TOPIC_LEN 32

typedef struct {
    struct sockaddr_in addr;
    int is_publisher;
    int active;
    char topic[TOPIC_LEN]; // tema al que publica o está suscrito
} Client;

int main() {
    int sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    Client clients[MAX_CLIENTS] = {0};

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error creando socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en bind");
        exit(EXIT_FAILURE);
    }

    printf("Broker UDP escuchando en puerto %d...\n", PORT);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        int n = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0,
                         (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("Error en recvfrom");
            continue;
        }
        buffer[n] = '\0';

        // Buscar o registrar cliente
        int found = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active &&
                clients[i].addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                clients[i].addr.sin_port == client_addr.sin_port) {
                found = i;
                break;
            }
        }

        if (found == -1) {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].active) {
                    clients[i].addr = client_addr;
                    clients[i].active = 1;
                    clients[i].is_publisher = 0;
                    memset(clients[i].topic, 0, TOPIC_LEN);
                    found = i;
                    break;
                }
            }
        }

        if (found == -1) continue;

        // Formato de registro:
        // "PUBLISHER:partido1"
        // "SUBSCRIBER:partido1,partido2"  <- puede suscribirse a varios
        // Mensajes normales: "partido1:texto del mensaje"

        if (strncmp(buffer, "PUBLISHER:", 10) == 0) {
            clients[found].is_publisher = 1;
            strncpy(clients[found].topic, buffer + 10, TOPIC_LEN - 1);
            printf("Publisher registrado en tema [%s]\n", clients[found].topic);

        } else if (strncmp(buffer, "SUBSCRIBER:", 11) == 0) {
            clients[found].is_publisher = 0;
            strncpy(clients[found].topic, buffer + 11, TOPIC_LEN - 1);
            printf("Subscriber registrado en temas [%s]\n", clients[found].topic);

        } else {
            // Mensaje con formato "tema:contenido"
            char *sep = strchr(buffer, ':');
            if (sep == NULL) continue;

            char msg_topic[TOPIC_LEN] = {0};
            strncpy(msg_topic, buffer, sep - buffer);
            char *msg_content = sep + 1;

            if (clients[found].is_publisher) {
                int count = 0;
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (clients[j].active && !clients[j].is_publisher) {
                        // Verificar si el subscriber está suscrito a este tema
                        // El campo topic puede tener "partido1,partido2"
                        if (strstr(clients[j].topic, msg_topic) != NULL) {
                            sendto(sock, msg_content, strlen(msg_content), 0,
                                   (struct sockaddr *)&clients[j].addr,
                                   sizeof(clients[j].addr));
                            count++;
                        }
                    }
                }
                printf("Mensaje [%s] reenviado a %d suscriptor(es): %s\n",
                       msg_topic, count, msg_content);
            }
        }
    }

    close(sock);
    return 0;
}