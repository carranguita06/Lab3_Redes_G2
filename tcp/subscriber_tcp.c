#include <stdio.h>       // Para imprimir en consola (printf, perror)
#include <stdlib.h>      // Para utilidades generales
#include <string.h>      // Para manejar texto (strcpy, memset, strncpy)
#include <unistd.h>      // Para funciones de Linux como close()
#include <sys/types.h>   // [CORRECCIÓN] Includes explícitos (antes llegaban "de rebote")
#include <sys/socket.h>  // [CORRECCIÓN] socket, connect, send, recv
#include <netinet/in.h>  // [CORRECCIÓN] struct sockaddr_in
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
    int largo_acumulado = 0;                // [CORRECCIÓN] Cuántos bytes hay guardados que aún no forman una línea completa
    char tema_partido[32] = "PartidoA";     // El partido que sigo por defecto si no escribo nada en la terminal
    setvbuf(stdout, NULL, _IOLBF, 0);
    // Si al ejecutar el programa escribí un partido (ej: ./subscriber_tcp PartidoB), lo guardo aquí
    // (también se aceptan varios separados por coma: ./subscriber_tcp PartidoA,PartidoB)
    if (argc > 1) {
        strncpy(tema_partido, argv[1], sizeof(tema_partido) - 1);
    }

    // [CORRECCIÓN] Los saltos de línea romperían el protocolo (cada mensaje termina en '\n')
    if (strpbrk(tema_partido, "\r\n") != NULL) {
        fprintf(stderr, "Tema inválido: no puede contener saltos de línea\n");
        return -1;
    }

    // [CORRECCIÓN] IP del broker opcional como segundo argumento (por defecto, localhost).
    // Uso: ./subscriber_tcp PartidoB 192.168.1.10
    const char *ip_broker = "127.0.0.1";
    if (argc > 2) {
        ip_broker = argv[2];
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

    // Traduzco la IP (por defecto "127.0.0.1", localhost) a formato binario para que la red la entienda
    if (inet_pton(AF_INET, ip_broker, &direccion_broker.sin_addr) <= 0) {
        perror("Dirección IP inválida");
        close(socket_cliente);
        return -1;
    }

    // ------------------------------------------------------------------------
    //  CONECTAR AL BROKER (Hago la llamada)
    // ------------------------------------------------------------------------
    // connect() inicia la conexión TCP con el broker (aquí ocurre el three-way handshake)
    if (connect(socket_cliente, (struct sockaddr *)&direccion_broker, sizeof(direccion_broker)) < 0) {
        perror("Error conectando al broker");
        close(socket_cliente);
        return -1;
    }

    printf(">>> Conectado al broker como Hincha / Suscriptor de: [%s]\n", tema_partido);

    // ------------------------------------------------------------------------
    // SUSCRIBIRSE AL PARTIDO
    // ------------------------------------------------------------------------
    // Le aviso al broker qué partido me interesa usando el formato "SUB:NombrePartido"
    // [CORRECCIÓN] Termina en '\n' para que el broker sepa dónde acaba el mensaje (TCP es un flujo de bytes).
    snprintf(buffer_noticia, TAMANO_BUFFER, "SUB:%s\n", tema_partido);
    if (send(socket_cliente, buffer_noticia, strlen(buffer_noticia), MSG_NOSIGNAL) < 0) {
        perror("Error enviando la suscripción");
        close(socket_cliente);
        return -1;
    }

    printf(">>> Esperando eventos del partido en vivo...\n\n");

    // ------------------------------------------------------------------------
    // ESCUCHAR NOTICIAS (Bucle infinito esperando mensajes)
    // ------------------------------------------------------------------------
    while (1) {
        // [CORRECCIÓN] Si el buffer se llenó sin ver ningún '\n', el mensaje es inválido:
        // lo descartamos para no quedarnos sin espacio.
        if (largo_acumulado >= TAMANO_BUFFER - 1) {
            printf("[AVISO] Mensaje demasiado largo sin '\\n', descartado\n");
            largo_acumulado = 0;
        }

        // recv() frena el programa aquí hasta que llegue un mensaje del broker por la red
        // [CORRECCIÓN] Se piden como máximo TAMANO_BUFFER - 1 bytes (queda 1 libre para el '\0';
        // antes se pedían 1024 y luego se escribía en la posición 1024, fuera del arreglo).
        // Además los bytes nuevos se guardan AL FINAL de lo que ya teníamos acumulado.
        int bytes_recibidos = recv(socket_cliente,
                                   buffer_noticia + largo_acumulado,
                                   TAMANO_BUFFER - 1 - largo_acumulado, 0);

        // Si recv() devuelve 0 o menos, el broker cerró o se cayó la red
        if (bytes_recibidos <= 0) {
            printf("\n>>> El broker cerró la conexión.\n");
            break;
        }

        largo_acumulado += bytes_recibidos;

        // [CORRECCIÓN] TCP no respeta los límites de los mensajes: en un solo recv() pueden
        // llegar varias noticias pegadas, o una noticia partida en dos. Por eso sacamos del
        // buffer cada línea COMPLETA (terminada en '\n') y la imprimimos por separado.
        char *inicio = buffer_noticia;
        char *fin_buffer = buffer_noticia + largo_acumulado;
        char *salto;

        while ((salto = memchr(inicio, '\n', fin_buffer - inicio)) != NULL) {
            // Le pongo el terminador '\0' al final para que C sepa dónde termina el texto
            *salto = '\0';

            // Imprimo la noticia deportiva recibida (gol, tarjeta o cambio)
            printf("[NOTICIA RECIBIDA] %s\n", inicio);

            inicio = salto + 1;   // Paso a la siguiente línea
        }

        // Lo que sobró (una línea incompleta) se mueve al principio y esperamos el resto
        int restante = (int)(fin_buffer - inicio);
        memmove(buffer_noticia, inicio, restante);
        largo_acumulado = restante;
    }

    // ------------------------------------------------------------------------
    // CERRAR EL CANAL
    // ------------------------------------------------------------------------
    close(socket_cliente);
    return 0;
}
