// ============================================================================
// 1. LIBRERÍAS
// Herramientas necesarias del sistema operativo Linux para sockets y memoria
// ============================================================================
#include <stdio.h>       // Para imprimir en consola (printf, perror)
#include <stdlib.h>      // Para control del programa (exit) y memoria
#include <string.h>      // Para manipular cadenas de texto (memset, strlen, strncmp)
#include <errno.h>       // [CORRECCIÓN] Para revisar errno (EINTR) tras select()/send()
#include <unistd.h>      // Para llamadas de Linux como close()
#include <sys/types.h>   // [CORRECCIÓN] Includes explícitos (antes llegaban "de rebote")
#include <sys/socket.h>  // [CORRECCIÓN] socket, bind, listen, accept, send, recv
#include <sys/select.h>  // [CORRECCIÓN] select() y fd_set
#include <netinet/in.h>  // [CORRECCIÓN] struct sockaddr_in
#include <arpa/inet.h>   // Para estructuras de red IPv4 y puertos (htons)

#define PUERTO 8080           // El puerto donde el broker abrirá su ventanilla
#define MAX_CLIENTES 20       // Capacidad máxima de conexiones simultáneas
#define TAMANO_BUFFER 1024    // Tamaño de la caja de texto para recibir mensajes
#define TAMANO_TEMA 32        // Tamaño máximo para el nombre del partido

// ============================================================================
// PROTOCOLO (explicación importante)
// TCP es un FLUJO de bytes, no una lista de mensajes: dos send() seguidos pueden
// llegar pegados en un solo recv(), o un send() puede llegar partido en dos.
// [CORRECCIÓN] Por eso ahora CADA mensaje termina con un salto de línea '\n'
// y el broker acumula lo recibido por cliente hasta tener líneas completas.
//   Cliente -> Broker:  "PUBLISHER\n"  |  "SUB:PartidoA,PartidoB\n"  |  "Tema|Noticia\n"
//   Broker  -> Cliente: "Tema|Noticia\n"
// ============================================================================

// [CORRECCIÓN] La "libreta de control" pasa a ser global (static) para que las
// funciones auxiliares puedan usarla sin recibir 5 parámetros.
// Arreglos paralelos: la posición i describe al mismo cliente en todos ellos.
static int sockets_clientes[MAX_CLIENTES] = {0};            // Guarda el identificador numérico de cada cliente
static int es_publicador[MAX_CLIENTES] = {0};               // 1 si es periodista/publicador, 0 si es hincha/suscriptor
static char temas_suscritos[MAX_CLIENTES][TAMANO_TEMA];     // Qué partido sigue cada suscriptor
static char acumulado[MAX_CLIENTES][TAMANO_BUFFER];         // [CORRECCIÓN] Bytes recibidos que aún no forman una línea completa
static int largo_acumulado[MAX_CLIENTES] = {0};             // [CORRECCIÓN] Cuántos bytes hay guardados en 'acumulado' por cliente

// Comprueba si "tema" está en la lista separada por comas del suscriptor
// (ej: tema_interesado("PartidoA,PartidoB", "PartidoA") -> 1).
// Tokenización exacta con strcmp: evita falsos positivos por substring.
int tema_interesado(const char *lista_temas, const char *tema) {
    char copia[TAMANO_TEMA];
    strncpy(copia, lista_temas, TAMANO_TEMA - 1);
    copia[TAMANO_TEMA - 1] = '\0';

    char *token = strtok(copia, ",");
    while (token != NULL) {
        if (strcmp(token, tema) == 0) return 1;
        token = strtok(NULL, ",");
    }
    return 0;
}

// [CORRECCIÓN] Envía TODOS los bytes pedidos. send() puede enviar solo una parte,
// así que repetimos hasta terminar. MSG_NOSIGNAL evita que el broker muera con
// SIGPIPE si el suscriptor ya cerró su conexión (en vez de eso send() devuelve -1).
int enviar_todo(int socket_destino, const char *datos, size_t largo) {
    size_t enviados = 0;
    while (enviados < largo) {
        ssize_t n = send(socket_destino, datos + enviados, largo - enviados, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;   // Nos interrumpió una señal: reintentamos
            return -1;                      // Error real (cliente caído, etc.)
        }
        enviados += (size_t)n;
    }
    return 0;
}

// [CORRECCIÓN] Función única de reenvío. Antes había dos bloques copiados con
// reglas distintas (uno revisaba es_publicador y contaba, el otro no).
// - tema:     partido de la noticia (se ignora si a_todos == 1)
// - a_todos:  1 = entregar a todos los suscriptores registrados (noticia general)
// Devuelve a cuántos suscriptores se entregó.
int reenviar(const char *linea, const char *tema, int a_todos) {
    char mensaje[TAMANO_BUFFER + 2];
    // Volvemos a poner el '\n' para que el suscriptor pueda separar mensajes
    int largo = snprintf(mensaje, sizeof(mensaje), "%s\n", linea);
    int count = 0;

    for (int j = 0; j < MAX_CLIENTES; j++) {
        int destino = sockets_clientes[j];

        // Saltamos casillas vacías, publicadores y clientes que aún no se han registrado
        if (destino <= 0 || es_publicador[j] || temas_suscritos[j][0] == '\0') continue;

        if (a_todos ||
            strcmp(temas_suscritos[j], "TODOS") == 0 ||
            tema_interesado(temas_suscritos[j], tema)) {
            if (enviar_todo(destino, mensaje, (size_t)largo) == 0) {
                count++;   // Se incrementa DESPUÉS de cada send exitoso
            }
        }
    }
    return count;
}

// [CORRECCIÓN] Procesa UNA línea completa (ya sin el '\n') del cliente en la posición i.
void procesar_linea(int i, char *linea) {
    int socket_actual = sockets_clientes[i];

    // Si el mensaje venía de una terminal tipo Windows ("\r\n"), quitamos el '\r'
    size_t largo = strlen(linea);
    if (largo > 0 && linea[largo - 1] == '\r') linea[largo - 1] = '\0';

    if (linea[0] == '\0') return;   // Línea vacía: no hay nada que hacer

    // --------------------------------------------------------
    // SUB-CASO B1: El cliente se presenta como PUBLICADOR
    // [CORRECCIÓN] Ahora se compara el mensaje COMPLETO (strcmp) y no solo el
    // prefijo, para no confundir una noticia que empiece por "PUBLISHER".
    // --------------------------------------------------------
    if (strcmp(linea, "PUBLISHER") == 0) {
        es_publicador[i] = 1;
        printf("[REGISTRO] Socket %d se identificó como PUBLICADOR (Periodista)\n", socket_actual);
    }
    // --------------------------------------------------------
    // SUB-CASO B2: Es un mensaje con una noticia deportiva
    // [CORRECCIÓN] Se revisa ANTES que los registros de suscriptor: si ya sabemos
    // que es publicador, todo lo que mande es noticia.
    // --------------------------------------------------------
    else if (es_publicador[i] == 1) {
        char tema_mensaje[TAMANO_TEMA] = {0};
        char *separador = strchr(linea, '|');

        // Si el mensaje viene en formato "Tema|Noticia" (ej: "PartidoA|Gol...")
        if (separador != NULL) {
            int longitud_tema = separador - linea;
            if (longitud_tema >= TAMANO_TEMA) {
                // [CORRECCIÓN] Antes un tema demasiado largo quedaba vacío en silencio
                printf("[ERROR] Tema demasiado largo, mensaje descartado\n");
                return;
            }
            strncpy(tema_mensaje, linea, longitud_tema);

            printf("[NOTICIA] De [%s]: %s\n", tema_mensaje, separador + 1);

            // Reenviamos SOLO a los suscriptores interesados en este partido
            int count = reenviar(linea, tema_mensaje, 0);
            printf("         -> entregado a %d suscriptor(es)\n", count);
        }
        // Si el mensaje no trae separador '|', reenvía a todos los suscriptores
        // [CORRECCIÓN] Antes el comentario decía "a todos" pero, con tema vacío,
        // solo lo recibían los suscriptores "TODOS". Ahora sí va a todos.
        else {
            printf("[NOTICIA GENERAL]: %s\n", linea);
            int count = reenviar(linea, "", 1);
            printf("         -> entregado a %d suscriptor(es)\n", count);
        }
    }
    // --------------------------------------------------------
    // SUB-CASO B3: El cliente se suscribe a un tema (SUB:PartidoA)
    // --------------------------------------------------------
    else if (strncmp(linea, "SUB:", 4) == 0) {
        es_publicador[i] = 0;
        memset(temas_suscritos[i], 0, TAMANO_TEMA);
        strncpy(temas_suscritos[i], linea + 4, TAMANO_TEMA - 1);
        printf("[REGISTRO] Socket %d suscrito al tema: '%s'\n", socket_actual, temas_suscritos[i]);
    }
    // Compatibilidad por si el suscriptor solo envía la palabra "SUBSCRIBER"
    else if (strcmp(linea, "SUBSCRIBER") == 0) {
        es_publicador[i] = 0;
        strcpy(temas_suscritos[i], "TODOS");
        printf("[REGISTRO] Socket %d registrado como SUSCRIPTOR general\n", socket_actual);
    }
    else {
        printf("[AVISO] Socket %d envió un mensaje no reconocido: '%s'\n", socket_actual, linea);
    }
}

int main() {
    // En Linux, un socket se representa con un int simple
    // socket_servidor: El número de socket principal del broker
    // nuevo_socket: El número temporal asignado a un cliente que se acaba de conectar
    // max_descriptor: El número de socket más alto (select lo necesita para saber hasta dónde buscar)
    // actividad: Almacena cuántos sockets tienen mensajes listos para leer tras llamar a select()
    setvbuf(stdout, NULL, _IOLBF, 0);
    int socket_servidor, nuevo_socket, max_descriptor, actividad;

    // Inicializamos la libreta de temas vacía
    memset(temas_suscritos, 0, sizeof(temas_suscritos));
    memset(acumulado, 0, sizeof(acumulado));

    // Estructura con los datos de red del broker (IP y Puerto)
    struct sockaddr_in direccion_servidor;
    socklen_t tamano_direccion = sizeof(direccion_servidor);   // [CORRECCIÓN] socklen_t evita el cast

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
        perror("El puerto podría estar ocupado");
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

        // [CORRECCIÓN] Si select() falla, el contenido de descriptores_lectura es
        // indefinido: NO se debe seguir usándolo. Reintentamos desde el inicio.
        if (actividad < 0) {
            if (errno == EINTR) continue;   // Interrumpido por una señal: no es grave
            perror("Error en la llamada a select");
            exit(EXIT_FAILURE);
        }

        // ====================================================================
        // CASO A: ALGUIEN TOCÓ EL TIMBRE (Nueva conexión entrante)
        // ====================================================================
        if (FD_ISSET(socket_servidor, &descriptores_lectura)) {
            // accept() contesta la llamada y le asigna un socket exclusivo a esa conexión
            nuevo_socket = accept(socket_servidor, (struct sockaddr *)&direccion_servidor, &tamano_direccion);

            // [CORRECCIÓN] Un accept() fallido ya no tumba el broker para todos los clientes
            if (nuevo_socket < 0) {
                perror("Error al aceptar nueva conexión");
            } else {
                printf("\n[+] Nueva conexión aceptada con descriptor: %d\n", nuevo_socket);

                // Guardamos el nuevo cliente en la primera posición libre de la lista
                int guardado = 0;
                for (int i = 0; i < MAX_CLIENTES; i++) {
                    if (sockets_clientes[i] == 0) {
                        sockets_clientes[i] = nuevo_socket;
                        es_publicador[i] = 0;
                        memset(temas_suscritos[i], 0, TAMANO_TEMA);
                        largo_acumulado[i] = 0;
                        guardado = 1;
                        break;
                    }
                }

                // [CORRECCIÓN] Si la libreta está llena, antes el socket quedaba abierto
                // y sin atender (fuga de descriptor). Ahora lo rechazamos cerrándolo.
                if (!guardado) {
                    printf("[!] Broker lleno (%d clientes). Conexión rechazada.\n", MAX_CLIENTES);
                    close(nuevo_socket);
                }
            }
        }

        // ====================================================================
        // CASO B: UN CLIENTE EXISTENTE ENVIÓ INFORMACIÓN O SE DESCONECTÓ
        // ====================================================================
        for (int i = 0; i < MAX_CLIENTES; i++) {
            int socket_actual = sockets_clientes[i];

            // [CORRECCIÓN] Saltamos las casillas vacías antes de llamar a FD_ISSET
            if (socket_actual <= 0) continue;

            // Revisamos si este cliente en particular tiene datos pendientes
            if (FD_ISSET(socket_actual, &descriptores_lectura)) {

                // [CORRECCIÓN] Dejamos 1 byte libre para el '\0' final: antes se leían
                // hasta 1024 bytes y luego se escribía en buffer[1024] (fuera del arreglo).
                int espacio = TAMANO_BUFFER - 1 - largo_acumulado[i];
                if (espacio <= 0) {
                    // Llegaron ~1 KB sin ningún '\n': mensaje inválido, empezamos de nuevo
                    printf("[AVISO] Socket %d: mensaje demasiado largo sin '\\n', descartado\n", socket_actual);
                    largo_acumulado[i] = 0;
                    espacio = TAMANO_BUFFER - 1;
                }

                // recv() lee los bytes que el cliente mandó a través de la red.
                // [CORRECCIÓN] Los guardamos AL FINAL de lo que ya teníamos acumulado.
                int bytes_recibidos = recv(socket_actual, acumulado[i] + largo_acumulado[i], espacio, 0);

                // Si recv() devuelve 0 o menos, significa que el cliente cerró su programa
                if (bytes_recibidos <= 0) {
                    printf("[-] Cliente desconectado (socket %d)\n", socket_actual);
                    close(socket_actual);
                    sockets_clientes[i] = 0;
                    es_publicador[i] = 0;
                    memset(temas_suscritos[i], 0, TAMANO_TEMA);
                    largo_acumulado[i] = 0;
                }
                else {
                    largo_acumulado[i] += bytes_recibidos;

                    // [CORRECCIÓN] Sacamos del acumulado TODAS las líneas completas
                    // (terminadas en '\n'). Así funciona aunque lleguen varios mensajes
                    // pegados o un mensaje partido en dos recv().
                    char *inicio = acumulado[i];
                    char *fin_buffer = acumulado[i] + largo_acumulado[i];
                    char *salto;

                    while ((salto = memchr(inicio, '\n', fin_buffer - inicio)) != NULL) {
                        *salto = '\0';                // Convertimos la línea en un string de C
                        procesar_linea(i, inicio);    // La procesamos (registro o noticia)
                        inicio = salto + 1;           // Pasamos a la siguiente línea
                    }

                    // Lo que sobró (una línea incompleta) se mueve al principio y esperamos el resto
                    int restante = (int)(fin_buffer - inicio);
                    memmove(acumulado[i], inicio, restante);
                    largo_acumulado[i] = restante;
                }
            }
        }
    }

    return 0;
}
