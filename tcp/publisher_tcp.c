#include <stdio.h>       // Para imprimir en consola (printf, perror) y formatear texto (sprintf)
#include <stdlib.h>      // Para utilidades del sistema como salir del programa (exit) y números aleatorios (rand)
#include <string.h>      // Para manipular cadenas de texto (strlen, strcpy, memset)
#include <unistd.h>      // Para funciones del so Linux como sleep() y close()
#include <arpa/inet.h>   // Para funciones de red e Internet (conversión de IPs y puertos)
#include <time.h>        // Para obtener la hora actual y usarla como semilla de aleatoriedad

#define PUERTO 8080            // El puerto donde el broker está escuchando
#define TAMANO_BUFFER 1024     // Tamaño en bytes para guardar texto

int main(int argc, char *argv[]) {
    // En Linux, un socket se representa con un int simple.
    // Es como el identificador único o "número de llamada" de nuestra conexión.
    int socket_cliente;

    // Esta estructura es donde anotamos los datos del destinatario:
    // a qué familia pertenece (IPv4), qué puerto tiene y cuál es su dirección IP.
    struct sockaddr_in direccion_broker;

    // ahora el mensaje es una cadena de chars con tamaño 1024
    char buffer_mensaje[TAMANO_BUFFER];

    // Aquí declaramos tema_partido como default "PartidoA", esto en caso de que no se declare nada en la consola
    // Pero si se le da un nombre copia lo de la consola y lo escribe en la variable tema_partido
    char tema_partido[32] = "PartidoA";
    if (argc > 1) {
        strncpy(tema_partido, argv[1], sizeof(tema_partido) - 1);
    }

    // ------------------------------------------------------------------------
    // CREAR EL SOCKET
    // ------------------------------------------------------------------------
    // AF_INET      -> Usamos direcciones IPv4.
    // SOCK_STREAM  -> Usamos TCP (orientado a conexión, confiable y ordenado).
    // 0            -> Protocolo por defecto (IP).
    // Si la función devuelve un número menor a 0, hubo un error al crearlo.
    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_cliente < 0) {
        perror("Error al crear el socket");
        return -1;
    }

    // ------------------------------------------------------------------------
    // LLENAR LA struct direccion_broker; del Broker)
    // ------------------------------------------------------------------------
    // Limpiamos la estructura llenándola de ceros para evitar basura en memoria
    memset(&direccion_broker, 0, sizeof(direccion_broker));

    // Indicamos que nos conectaremos por IPv4
    direccion_broker.sin_family = AF_INET;

    // htons = "Host to Network Short". Convierte el número 8080 al orden de bytes (en Big Endian)
    direccion_broker.sin_port = htons(PUERTO);

    // inet_pton = "Presentation to Numeric". Traduce la IP en texto humano ("127.0.0.1")
    // a su representación binaria que la tarjeta de red entiende.
    if (inet_pton(AF_INET, "127.0.0.1", &direccion_broker.sin_addr) <= 0) {
        perror("Dirección IP inválida o no soportada");
        return -1;
    }

    // ------------------------------------------------------------------------
    // CONECTARSE AL BROKER (Iniciar el canal TCP)
    // ------------------------------------------------------------------------
    // connect() llama al kernel de Linux para buscar al broker.
    // AQUÍ es donde el sistema operativo ejecuta el 3-Way Handshake
    // (SYN -> SYN/ACK -> ACK)
    if (connect(socket_cliente, (struct sockaddr *)&direccion_broker, sizeof(direccion_broker)) < 0) {
        perror("Error al conectar con el broker");
        return -1;
    }

    printf(">>> Conectado exitosamente al broker como Periodista del [%s]\n", tema_partido);

    // ------------------------------------------------------------------------
    // PRESENTARSE ANTE EL BROKER
    // ------------------------------------------------------------------------
    // Le enviamos un primer mensaje de texto diciendo que somos un publicador 
    // para que el broker sepa qué rol jugamos y no nos confunda con un hincha/suscriptor.
    strcpy(buffer_mensaje, "PUBLISHER");
    send(socket_cliente, buffer_mensaje, strlen(buffer_mensaje), 0);

    // Pausamos 1 segundo para darle tiempo al broker de procesar nuestro registro
    sleep(1);

    // ------------------------------------------------------------------------
    // GENERAR Y TRANSMITIR LOS 10 EVENTOS DEL PARTIDO
    // ------------------------------------------------------------------------
    // Aquí estamos usando la hora actual y el ID del proceso para generar los mensajes aleatorios
    srand(time(NULL) ^ getpid());

    for (int i = 1; i <= 10; i++) {
        // memset llena la caja de ceros ('\0') para borrar lo que escribimos en la vuelta anterior.
        memset(buffer_mensaje, 0, TAMANO_BUFFER);

        // Elegimos un número al azar entre 0, 1 y 2 para variar el tipo de noticia deportiva
        int tipo_evento = rand() % 3;

        // snprintf lo usamos para evitar el error de buffer overflows por lo que aquí si definimos el tamaño de lo que imprimimos
        // Toma variables y las concatena dentro de un texto.
        // Usamos el separador '|' para que el broker sepa qué parte es el tema y qué parte es la noticia.
        if (tipo_evento == 0) {
            snprintf(buffer_mensaje, TAMANO_BUFFER, "%s|Gol del equipo local al minuto %d", tema_partido, i * 7);
        } else if (tipo_evento == 1) {
            snprintf(buffer_mensaje, TAMANO_BUFFER, "%s|Cambio de jugador en el minuto %d", tema_partido, i * 6);
        } else {
            snprintf(buffer_mensaje, TAMANO_BUFFER, "%s|Tarjeta amarilla al número 10 en el minuto %d", tema_partido, i * 5);
        }

        // send() toma los bytes del mensaje y los lleva hacia el broker.
        send(socket_cliente, buffer_mensaje, strlen(buffer_mensaje), 0);

        printf("[Evento %d/10 enviado]: %s\n", i, buffer_mensaje);

        // Esperamos 2 segundos antes de mandar el siguiente gol/cambio
        // para que se aprecie la llegada en tiempo real en la pantalla del hincha.
        sleep(2);
    }

    printf(">>> Todos los reportes del partido fueron enviados con éxito.\n");

    // ------------------------------------------------------------------------
    // CERRAR LA CONEXIÓN
    // ------------------------------------------------------------------------
    // Al cerrar el socket, el sistema operativo envía el paquete [FIN] de TCP
    // para avisarle al broker que terminamos y liberar la memoria de la red.
    close(socket_cliente);
    return 0;
}