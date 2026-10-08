#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 8080
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024

int main() {
    int server_fd, new_socket, max_sd, activity;
    int client_sockets[MAX_CLIENTS] = {0};
    int is_publisher[MAX_CLIENTS] = {0}; // 1 = publisher, 0 = subscriber
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    fd_set readfds;

    // Crear socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Error creando socket");
        exit(EXIT_FAILURE);
    }

    // 2. Configurar dirección
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 3. Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        exit(EXIT_FAILURE);
    }

    // 4. Listen
    if (listen(server_fd, 10) < 0) {
        perror("Error en listen");
        exit(EXIT_FAILURE);
    }

    printf("Broker escuchando en puerto %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);

        // Añadir socket servidor
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        // Añadir sockets de clientes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0)
                FD_SET(sd, &readfds);

            if (sd > max_sd)
                max_sd = sd;
        }

        // Esperar actividad
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("Error en select");
        }

        // Nueva conexión
        if (FD_ISSET(server_fd, &readfds)) {
            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);

            if (new_socket < 0) {
                perror("Error en accept");
                exit(EXIT_FAILURE);
            }

            printf("Nueva conexión: socket %d\n", new_socket);

            // Añadir a lista
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    break;
                }
            }
        }

        // Manejar clientes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];

            if (FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, BUFFER_SIZE);

                int valread = recv(sd, buffer, BUFFER_SIZE, 0);

                // Cliente desconectado
                if (valread <= 0) {
                    printf("Cliente desconectado: socket %d\n", sd);
                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    buffer[valread] = '\0';

                    // Si aún no está identificado
                    if (strncmp(buffer, "PUBLISHER", 9) == 0) {
                        is_publisher[i] = 1;
                        printf("Socket %d registrado como PUBLISHER\n", sd);
                    } else if (strncmp(buffer, "SUBSCRIBER", 10) == 0) {
                        is_publisher[i] = 0;
                        printf("Socket %d registrado como SUBSCRIBER\n", sd);
                    } else {
                        // Mensaje de publisher → reenviar
                        if (is_publisher[i] == 1) {
                            printf("Mensaje recibido: %s\n", buffer);

                            for (int j = 0; j < MAX_CLIENTS; j++) {
                                if (client_sockets[j] != 0 && is_publisher[j] == 0) {
                                    send(client_sockets[j], buffer, strlen(buffer), 0);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}