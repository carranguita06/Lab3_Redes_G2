#include <stdio.h>       // Para imprimir en consola (printf, perror)
#include <stdlib.h>      // Para utilidades generales
#include <string.h>      // Para manejar texto (strcpy, memset, strncpy)
#include <unistd.h>      // Para funciones de Linux como close()
#include <arpa/inet.h>   // Para manejar direcciones IP y puertos

#define PUERTO 8080           // El puerto del broker al que nos conectamos
#define TAMANO_BUFFER 1024    // Espacio en memoria para guardar el texto que llega

int main(int argc, char *argv[]) {
    // ------------------------------------------------------------------------
    // VARIABLES
    // ------------------------------------------------------------------------
    int socket_cliente;                     // El número identificador de nuestra conexión
    struct sockaddr_in direccion_broker;    // La "ficha" con los datos del broker (IP y puerto)
    char buffer_noticia[TAMANO_BUFFER];     // La caja donde guardamos lo que nos manda el broker
    char tema_partido[32] = "PartidoA";     // El partido que sigo por defecto si no escribo nada en la terminal[cite: 3]

    // Si al ejecutar el programa escribí un partido (ej: ./subscriber_tcp PartidoB), lo guardo aquí[cite: 3]
    if (argc > 1) {
        strncpy(tema_partido, argv[1], sizeof(tema_partido) - 1);
    }

    // ------------------------------------------------------------------------
    // CREAR EL SOCKET (Abro el canal de red)
    // ------------------------------------------------------------------------
    // Le pido a Linux un socket IPv4 (AF_INET) y de tipo TCP (SOCK_STREAM)
    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_cliente < 0) {
        perror("Error creando el socket");
        return -1;
    }

    // ------------------------------------------------------------------------
    // DATOS DEL BROKER (A dónde me voy a conectar)
    // ------------------------------------------------------------------------
    memset(&direccion_broker, 0, sizeof(direccion_broker)); // Limpio la estructura para que no tenga basura
    direccion_broker.sin_family = AF_INET;                 // Protocolo IPv4
    direccion_broker.sin_port = htons(PUERTO);             // Convierto el puerto 8080 al formato de red

    // Traduzco la IP "127.0.0.1" (localhost) a formato binario para que la red la entienda
    if (inet_pton(AF_INET, "127.0.0.1", &direccion_broker.sin_addr) <= 0) {
        perror("Dirección IP inválida");
        return -1;
    }

    // ------------------------------------------------------------------------
    //  CONECTAR AL BROKER (Hago la llamada)
    // ------------------------------------------------------------------------
    // connect() inicia la conexión TCP con el broker (aquí ocurre el three-way handshake)[cite: 2, 34]
    if (connect(socket_cliente, (struct sockaddr *)&direccion_broker, sizeof(direccion_broker)) < 0) {
        perror("Error conectando al broker");
        return -1;
    }

    printf(">>> Conectado al broker como Hincha / Suscriptor de: [%s]\n", tema_partido);

    // ------------------------------------------------------------------------
    // SUSCRIBIRSE AL PARTIDO
    // ------------------------------------------------------------------------
    // Le aviso al broker qué partido me interesa usando el formato "SUB:NombrePartido"
    snprintf(buffer_noticia, TAMANO_BUFFER, "SUB:%s", tema_partido);
    send(socket_cliente, buffer_noticia, strlen(buffer_noticia), 0);

    printf(">>> Esperando eventos del partido en vivo...\n\n");

    // ------------------------------------------------------------------------
    // ESCUCHAR NOTICIAS (Bucle infinito esperando mensajes)
    // ------------------------------------------------------------------------
    while (1) {
        // Limpio el buffer antes de recibir algo nuevo
        memset(buffer_noticia, 0, TAMANO_BUFFER);

        // recv() frena el programa aquí hasta que llegue un mensaje del broker por la red[cite: 3, 4]
        int bytes_recibidos = recv(socket_cliente, buffer_noticia, TAMANO_BUFFER, 0);

        // Si recv() devuelve 0 o menos, el broker cerró o se cayó la red
        if (bytes_recibidos <= 0) {
            printf("\n>>> El broker cerró la conexión.\n");
            break;
        }

        // Le pongo el terminador '\0' al final para que C sepa dónde termina el texto
        buffer_noticia[bytes_recibidos] = '\0';

        // Imprimo la noticia deportiva recibida (gol, tarjeta o cambio)[cite: 3]
        printf("[NOTICIA RECIBIDA] %s\n", buffer_noticia);
    }

    // ------------------------------------------------------------------------
    // CERRAR EL CANAL
    // ------------------------------------------------------------------------
    close(socket_cliente);
    return 0;
}