// ============================================================================
// 1. LIBRERÍAS
// Herramientas necesarias del sistema operativo Linux para sockets y memoria
// ============================================================================
#include <stdio.h>       // Para imprimir en consola (printf, perror)
#include <stdlib.h>      // Para control del programa (exit) y memoria
#include <string.h>      // Para manipular cadenas de texto (memset, strlen, strncmp)
#include <unistd.h>      // Para llamadas de Linux como close()
#include <arpa/inet.h>   // Para estructuras de red IPv4 y puertos (htons)
#include <time.h>        // Para obtener la hora actual

#define PUERTO 8080           // El puerto donde el broker abrirá su ventanilla
#define MAX_CLIENTES 20       // Capacidad máxima de conexiones simultáneas
#define TAMANO_BUFFER 1024    // Tamaño de la caja de texto para recibir mensajes
#define TAMANO_TEMA 32        // Tamaño máximo para el nombre del partido 

int main() {
    // En Linux, un socket se representa con un int simple
    // socket_servidor: El número de socket principal del broker
    // nuevo_socket: El número temporal asignado a un cliente que se acaba de conectar
    // max_descriptor: El número de socket más alto (select lo necesita para saber hasta dónde buscar)
    // actividad: Almacena cuántos sockets tienen mensajes listos para leer tras llamar a select()
    int socket_servidor, nuevo_socket, max_descriptor, actividad;

    // Arreglos paralelos para llevar la "libreta de control" de cada cliente conectado:
    int sockets_clientes[MAX_CLIENTES] = {0};            // Guarda el identificador numérico de cada cliente
    int es_publicador[MAX_CLIENTES] = {0};               // 1 si es periodista/publicador, 0 si es hincha/suscriptor
    char temas_suscritos[MAX_CLIENTES][TAMANO_TEMA];     // Qué partido sigue cada suscriptor

    // Inicializamos la libreta de temas vacía
    memset(temas_suscritos, 0, sizeof(temas_suscritos));

    // Estructura con los datos de red del broker (IP y Puerto)
    struct sockaddr_in direccion_servidor;
    int tamano_direccion = sizeof(direccion_servidor);

    // Caja de texto temporal donde aterrizan los bytes que llegan de la red
    char buffer_recepcion[TAMANO_BUFFER];

    // fd_set es un conjunto de bits. Piensa en esto como una lista de verificación
    // donde marcamos con una 'X' qué sockets queremos que el sistema operativo vigile
    fd_set descriptores_lectura;

    // Opción para reutilizar el puerto de inmediato sin bloqueos de espera
    int opcion = 1;

    // ------------------------------------------------------------------------
    // CREAR EL SOCKET
    // ------------------------------------------------------------------------
    // AF_INET      -> Usamos direcciones IPv4.
    // SOCK_STREAM  -> Usamos TCP (orientado a conexión, confiable y ordenado).
    // 0            -> Protocolo por defecto (IP).
    // Si la función devuelve un número menor a 0, hubo un error al crearlo.
    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        perror("Error creando el socket del broker");
        exit(EXIT_FAILURE);
    }

    // Le decimos a Linux que si cerramos el broker y lo volvemos a abrir de una vez,
    // nos permita reutilizar el puerto 8080 sin arrojar "Address already in use".
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opcion, sizeof(opcion));

    // ------------------------------------------------------------------------
    // CONFIGURAR LA IDENTIDAD DE RED DEL BROKER
    // ------------------------------------------------------------------------
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_addr.s_addr = INADDR_ANY;  // Escuchar por cualquier tarjeta de red disponible
    direccion_servidor.sin_port = htons(PUERTO);       // Convertir 8080 al orden de bytes de red

    // ------------------------------------------------------------------------
    // ASOCIAR EL SOCKET CON EL PUERTO
    // ------------------------------------------------------------------------
    // bind() adueña al proceso del puerto 8080 en el computador.
    if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("El puerto podría estar ocupado)");
        exit(EXIT_FAILURE);
    }

    // ------------------------------------------------------------------------
    // PONERSE EN MODO ESCUCHA
    // ------------------------------------------------------------------------
    // listen() deja la ventanilla abierta. El '10' es la fila máxima de clientes en espera.
    if (listen(socket_servidor, 10) < 0) {
        perror("Error en listen");
        exit(EXIT_FAILURE);
    }

    printf("==============================================\n");
    printf(" Broker TCP listo y escuchando en el puerto %d\n", PUERTO);
    printf("==============================================\n");

    // ========================================================================
    // BUCLE PRINCIPAL (El broker nunca se apaga; siempre atiende eventos)
    // ========================================================================
    while (1) {
        // Vaciamos la lista de vigilancia para armarla de nuevo en cada vuelta
        FD_ZERO(&descriptores_lectura);

        // Ponemos el socket principal en la lista (para detectar nuevos clientes conectándose)
        FD_SET(socket_servidor, &descriptores_lectura);
        max_descriptor = socket_servidor;

        // Metemos en la lista todos los clientes que ya están conectados
        for (int i = 0; i < MAX_CLIENTES; i++) {
            int socket_actual = sockets_clientes[i];

            // Si la casilla no está vacía (> 0), añadimos ese cliente a la vigilancia
            if (socket_actual > 0) {
                FD_SET(socket_actual, &descriptores_lectura);
            }

            // Linux necesita saber cuál es el número más alto de socket para optimizar la búsqueda
            if (socket_actual > max_descriptor) {
                max_descriptor = socket_actual;
            }
        }

        // --------------------------------------------------------------------
        // MULTIPLEXACIÓN CON select():
        // En lugar de congelarnos esperando a un cliente específico, select()
        // pausa el programa y le pide a Linux: "despiértame solo cuando alguno
        // de los clientes hable o cuando alguien toque el timbre".
        // --------------------------------------------------------------------
        actividad = select(max_descriptor + 1, &descriptores_lectura, NULL, NULL, NULL);

        if (actividad < 0) {
            perror("Error en la llamada a select");
        }

        // ====================================================================
        // CASO A: ALGUIEN TOCÓ EL TIMBRE (Nueva conexión entrante)
        // ====================================================================
        if (FD_ISSET(socket_servidor, &descriptores_lectura)) {
            // accept() contesta la llamada y le asigna un socket exclusivo a esa conexión
            nuevo_socket = accept(socket_servidor, (struct sockaddr *)&direccion_servidor, (socklen_t *)&tamano_direccion);

            if (nuevo_socket < 0) {
                perror("Error al aceptar nueva conexión");
                exit(EXIT_FAILURE);
            }

            printf("\n[+] Nueva conexión aceptada con descriptor: %d\n", nuevo_socket);

            // Guardamos el nuevo cliente en la primera posición libre de la lista
            for (int i = 0; i < MAX_CLIENTES; i++) {
                if (sockets_clientes[i] == 0) {
                    sockets_clientes[i] = nuevo_socket;
                    es_publicador[i] = 0;
                    memset(temas_suscritos[i], 0, TAMANO_TEMA);
                    break;
                }
            }
        }

        // ====================================================================
        // CASO B: UN CLIENTE EXISTENTE ENVIÓ INFORMACIÓN O SE DESCONECTÓ
        // ====================================================================
        for (int i = 0; i < MAX_CLIENTES; i++) {
            int socket_actual = sockets_clientes[i];

            // Revisamos si este cliente en particular tiene datos pendientes
            if (FD_ISSET(socket_actual, &descriptores_lectura)) {
                memset(buffer_recepcion, 0, TAMANO_BUFFER);

                // recv() lee los bytes que el cliente mandó a través de la red
                int bytes_recibidos = recv(socket_actual, buffer_recepcion, TAMANO_BUFFER, 0);

                // Si recv() devuelve 0 o menos, significa que el cliente cerró su programa
                if (bytes_recibidos <= 0) {
                    printf("[-] Cliente desconectado (socket %d)\n", socket_actual);
                    close(socket_actual);
                    sockets_clientes[i] = 0;
                    es_publicador[i] = 0;
                    memset(temas_suscritos[i], 0, TAMANO_TEMA);
                } 
                else {
                    // Ponemos el terminador nulo '\0' al final para tratarlo como string seguro
                    buffer_recepcion[bytes_recibidos] = '\0';

                    // --------------------------------------------------------
                    // SUB-CASO B1: El cliente se presenta como PUBLICADOR
                    // --------------------------------------------------------
                    if (strncmp(buffer_recepcion, "PUBLISHER", 9) == 0) {
                        es_publicador[i] = 1;
                        printf("[REGISTRO] Socket %d se identificó como PUBLICADOR (Periodista)\n", socket_actual);
                    }
                    // --------------------------------------------------------
                    // SUB-CASO B2: El cliente se suscribe a un tema (SUB:PartidoA)
                    // --------------------------------------------------------
                    else if (strncmp(buffer_recepcion, "SUB:", 4) == 0) {
                        es_publicador[i] = 0;
                        strncpy(temas_suscritos[i], buffer_recepcion + 4, TAMANO_TEMA - 1);
                        printf("[REGISTRO] Socket %d suscrito al tema: '%s'\n", socket_actual, temas_suscritos[i]);
                    }
                    // Compatibilidad por si el suscriptor solo envía la palabra "SUBSCRIBER"
                    else if (strncmp(buffer_recepcion, "SUBSCRIBER", 10) == 0) {
                        es_publicador[i] = 0;
                        strcpy(temas_suscritos[i], "TODOS");
                        printf("[REGISTRO] Socket %d registrado como SUSCRIPTOR general\n", socket_actual);
                    }
                    // --------------------------------------------------------
                    // SUB-CASO B3: Es un mensaje con una noticia deportiva
                    // --------------------------------------------------------
                    else if (es_publicador[i] == 1) {
                        char tema_mensaje[TAMANO_TEMA] = {0};
                        char *separador = strchr(buffer_recepcion, '|');

                        // Si el mensaje viene en formato "Tema|Noticia" (ej: "PartidoA|Gol...")
                        if (separador != NULL) {
                            int longitud_tema = separador - buffer_recepcion;
                            if (longitud_tema < TAMANO_TEMA) {
                                strncpy(tema_mensaje, buffer_recepcion, longitud_tema);
                            }

                            printf("[NOTICIA] De [%s]: %s\n", tema_mensaje, separador + 1);

                            // Reenviamos SOLO a los suscriptores interesados en este partido
                            for (int j = 0; j < MAX_CLIENTES; j++) {
                                int destino = sockets_clientes[j];

                                if (destino != 0 && es_publicador[j] == 0) {
                                    if (strcmp(temas_suscritos[j], tema_mensaje) == 0 || strcmp(temas_suscritos[j], "TODOS") == 0) {
                                        send(destino, buffer_recepcion, strlen(buffer_recepcion), 0);
                                    }
                                }
                            }
                        } 
                        // Si el mensaje no trae separador '|', reenvía a todos los suscriptores
                        else {
                            printf("[NOTICIA GENERAL]: %s\n", buffer_recepcion);
                            for (int j = 0; j < MAX_CLIENTES; j++) {
                                int destino = sockets_clientes[j];
                                if (destino != 0 && es_publicador[j] == 0) {
                                    send(destino, buffer_recepcion, strlen(buffer_recepcion), 0);
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