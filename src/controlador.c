/**
 *  @file controlador.c
 *  @brief Proceso servidor del sistema de reservas.
 * 
 *  Este  programa implementa el  Controlador de Reserva, encargado de  recibir,
 *  evaluar y responder las solicitudes enviadas por los agentes. El controlador
 *  gestiona  la  ocupación del parque,  simula el avance del tiempo y decide si
 *  una familia  puede  reservar en la hora  solicitada, debe reprogramarse o si
 *  la solicitud debe ser negada según las reglas del sistema.
 *  
 *      Concurrencia:
 *  El controlador usa dos hilos POSIX:
 *  - **Hilo de reloj:** avanza la hora simulada y muestra el estado.
 *  - **Hilo de recepción:** escucha continuamente peticiones de los agentes.
 *  
 *      Parámetros esperados:
 *   -i <horaInicio> Hora inicial de la simulación (7–19).
 *   -f <horaFin> Hora final de la simulación (7–19).
 *   -s <segundosHora> Cantidad de segundos que equivale a 1 hora simulada.
 *   -t <aforoMax> Límite de personas permitidas simultáneamente.
 *   -p <pipePrincipal> FIFO por el cual los agentes envían solicitudes.
 *  
 *  Este módulo actúa como el núcleo del sistema de reservas, gestionando
 *  simultáneamente tiempo, ocupación y comunicación con múltiples agentes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "comunes.h"
#include "../include/estructuras.h"

// Constantes.
#define MAX_HORAS 24
#define MAX_AGENTES 200

// Variables globales.
static int horaIniSim = 7;
static int horaFinSim = 19;
static int aforoMax = 50;
static int segHorasSim = 1;
static int ocupacion[MAX_HORAS + 2];

// Pipes de los agentes.
static char pipes_agentes[MAX_AGENTES][128];
static int total_agentes = 0;

// Estadísticas finales.
static int solicitudes_ok = 0;
static int solicitudes_extemporaneas = 0;
static int solicitudes_reprogramadas = 0;
static int solicitudes_negadas = 0;

// Pipe principal.
static const char *pipe_principal = NULL;

// Reloj global.
static int hora_actual = 0;

// Flag de terminación
static volatile int debe_terminar = 0;

// Mutex para proteger acceso concurrente.
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Registrar pipe de un agente si no está ya registrado.
static void registrar_pipe_agente(const char *pipeN) {
    if (!pipeN || pipeN[0] == '\0') { return; }

    for (int i = 0; i < total_agentes; i++) {
        if (strcmp(pipes_agentes[i], pipeN) == 0) {
            return;
        }
    }
    strncpy(pipes_agentes[total_agentes], pipeN, 127);
    total_agentes++;
}

// Verificar si se puede reservar en la hora dada.
static int puede_reservar_en_hora(int h, int personas) {
    if (h < horaIniSim) { return 0; }
    if (h + 1 > horaFinSim) { return 0; }

    return !(ocupacion[h] + personas > aforoMax || ocupacion[h+1] + personas > aforoMax);
}

// Buscar un bloque de 2 horas disponible desde 'inicio' en adelante.
static int buscar_bloque_dos_horas(int personas, int inicio) {
    if (inicio < horaIniSim) { inicio = horaIniSim; }

    for (int h = inicio; h <= horaFinSim - 1; h++) {
        if (puede_reservar_en_hora(h, personas)) {
            return h;
        }
    }
    return -1;
}

// Enviar respuesta al agente.
static void enviar_respuesta(const MensajeReserva *msg, const RespuestaControlador *resp) {
    registrar_pipe_agente(msg->pipe_respuesta);

    int fd = abrir_pipe_escritura(msg->pipe_respuesta);
    if (fd == -1) { return; }

    write(fd, resp, sizeof(*resp));
    close(fd);
}

// Procesar diferentes tipos de reservas.
static void procesar_reserva_ok(const MensajeReserva *msg) {
    ocupacion[msg->hora_solicitada] += msg->num_personas;
    ocupacion[msg->hora_solicitada + 1] += msg->num_personas;
    solicitudes_ok++;

    RespuestaControlador respuesta;
    respuesta.tipo = RESERVA_OK;
    respuesta.hora_asignada = msg->hora_solicitada;
    snprintf(respuesta.mensaje, 300, "Reserva OK para %s (%d personas) en %d-%d",
                msg->nombre_familia,
                msg->num_personas,
                msg->hora_solicitada,
                msg->hora_solicitada+2);
    enviar_respuesta(msg, &respuesta);
}

// Procesar reserva reprogramada a otras horas.
static void procesar_reserva_otras_horas(const MensajeReserva *msg, int nuevaH) {
    ocupacion[nuevaH] += msg->num_personas;
    ocupacion[nuevaH + 1] += msg->num_personas;
    solicitudes_reprogramadas++;

    RespuestaControlador respuesta;
    respuesta.tipo = RESERVA_OTRAS_HORAS;
    respuesta.hora_asignada = nuevaH;
    snprintf(respuesta.mensaje, 300, "Sin cupo en %d. Reprogramada a %d-%d",
                msg->hora_solicitada, nuevaH, nuevaH+2);
    enviar_respuesta(msg, &respuesta);
}

// Procesar reserva extemporánea.
static void procesar_reserva_extemporanea(const MensajeReserva *msg, int nuevaH) {
    ocupacion[nuevaH] += msg->num_personas;
    ocupacion[nuevaH + 1] += msg->num_personas;
    solicitudes_extemporaneas++;

    RespuestaControlador respuesta;
    respuesta.tipo = RESERVA_EXTEMPORANEA;
    respuesta.hora_asignada = nuevaH;
    snprintf(respuesta.mensaje, 300, "Hora solicitada ya pasó. Reprogramada a %d-%d",
                nuevaH, nuevaH+2);
    enviar_respuesta(msg, &respuesta);
}

// Procesar reserva negada.
static void procesar_reserva_negada(const MensajeReserva *msg, const char *razon) {
    solicitudes_negadas++;

    RespuestaControlador respuesta;
    respuesta.tipo = RESERVA_NEGADA;
    respuesta.hora_asignada = -1;
    snprintf(respuesta.mensaje, 300, "Reserva negada para %s: %s",
                msg->nombre_familia, razon);
    enviar_respuesta(msg, &respuesta);
}

// Imprimir estado actual de ocupación.
static void imprimir_estado(int h) {
    printf("\n======= HORA %d =======\n", h);

    if (h - 1 >= horaIniSim) {
        printf("SALEN (%d-%d): %d personas\n", h-1, h, ocupacion[h-1]);
    }

    if (h >= horaIniSim && h <= horaFinSim) {
        printf("ESTÁN (%d-%d): %d personas\n", h, h+1, ocupacion[h]);
    }
}

// Hilo de reloj que avanza la hora simulada.
void *hiloReloj(void *arg) {
    (void)arg;
    while (1) {
        sleep(segHorasSim);

        pthread_mutex_lock(&mutex);
        if (hora_actual > horaFinSim) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        imprimir_estado(hora_actual);
        hora_actual++;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

// Hilo de recepción de mensajes de reserva.
void *hiloRecepcion(void *arg) {
    (void)arg;

    // Habilitar cancelación asíncrona del hilo
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    // Abrir el pipe principal UNA SOLA VEZ
    int fd = abrir_pipe_lectura(pipe_principal);
    if (fd == -1) {
        fprintf(stderr, "[CONTROLADOR] Error abriendo pipe principal\n");
        return NULL;
    }

    // Ahora cambiar a modo NO BLOQUEANTE para las lecturas
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    while (!debe_terminar) {
        // ¿Ya se acabó la simulación?
        pthread_mutex_lock(&mutex);
        int fin = (hora_actual > horaFinSim);
        pthread_mutex_unlock(&mutex);

        if (fin) {
            break;
        }

        // Leer el tipo de mensaje primero.
        TipoMensaje tipo;
        ssize_t r = read(fd, &tipo, sizeof(tipo));

        if (r == -1 && errno == EAGAIN) {
            // No había datos aún
            struct timespec ts = {0, 50000000}; // 50ms
            nanosleep(&ts, NULL);
            continue;
        }

        if (r != sizeof(tipo)) {
            // Lectura incompleta o EOF
            struct timespec ts = {0, 50000000}; // 50ms
            nanosleep(&ts, NULL);
            continue;
        }

        // Procesar mensaje de saludo (HELLO).
        if (tipo == MSG_HOLA) {
            MensajeHola hola;

            // Ya leímos el campo "tipo", faltan los demás bytes.
            ssize_t r2 = read(fd, ((char*)&hola) + sizeof(tipo), sizeof(hola) - sizeof(tipo));

            if (r2 != sizeof(hola) - sizeof(tipo)) {
                continue;
            }

            pthread_mutex_lock(&mutex);
            printf("[CONTROLADOR] HELLO recibido de %s\n", hola.nombre_agente);
            registrar_pipe_agente(hola.pipe_respuesta);

            MensajeWelcome w;
            w.hora_actual = hora_actual;

            int fdw = abrir_pipe_escritura(hola.pipe_respuesta);
            if (fdw != -1) {
                write(fdw, &w, sizeof(w));
                close(fdw);
            }

            pthread_mutex_unlock(&mutex);
            continue;
        }

        // Procesar mensaje de reserva (RESERVA).
        if (tipo == MSG_RESERVA) {
            MensajeReserva msg;
            msg.tipo = tipo;

            // Ya leímos el campo "tipo", faltan los demás bytes.
            ssize_t r2 = read(fd, ((char*)&msg) + sizeof(tipo), sizeof(msg) - sizeof(tipo));

            if (r2 != sizeof(msg) - sizeof(tipo)) {
                continue;
            }
            pthread_mutex_lock(&mutex);

            printf("[CONTROLADOR] Petición: agente=%s familia=%s hora=%d personas=%d\n",
                    msg.nombre_agente,
                    msg.nombre_familia,
                    msg.hora_solicitada,
                    msg.num_personas);
            registrar_pipe_agente(msg.pipe_respuesta);

            // Validaciones y procesamiento de la reserva.
            if (msg.num_personas > aforoMax) {
                procesar_reserva_negada(&msg, "Grupo supera aforo máximo");
                pthread_mutex_unlock(&mutex);
                continue;
            }

            if (msg.hora_solicitada > horaFinSim) {
                procesar_reserva_negada(&msg, "Hora solicitada fuera del rango");
                pthread_mutex_unlock(&mutex);
                continue;
            }

            if (msg.hora_solicitada < hora_actual) {
                int nh = buscar_bloque_dos_horas(msg.num_personas, hora_actual);
                if (nh != -1) {
                    procesar_reserva_extemporanea(&msg, nh);
                } else {
                    procesar_reserva_negada(&msg, "Extemporánea y sin cupo");
                }
                pthread_mutex_unlock(&mutex);
                continue;
            }

            if (puede_reservar_en_hora(msg.hora_solicitada, msg.num_personas)) {
                procesar_reserva_ok(&msg);
                pthread_mutex_unlock(&mutex);
                continue;
            }

            int nh = buscar_bloque_dos_horas(msg.num_personas, hora_actual);
            if (nh != -1) {
                procesar_reserva_otras_horas(&msg, nh);
            } else {
                procesar_reserva_negada(&msg, "Sin bloques disponibles");
            }

            pthread_mutex_unlock(&mutex);
            continue;
        }
        // Mensaje desconocido.
        printf("[CONTROLADOR] Mensaje desconocido recibido.\n");
    }
    
    close(fd);
    return NULL;
}

// Reporte final al terminar la simulación.
static void reporte_final(void) {
    printf("\n====== REPORTE FINAL ======\n");
    printf(" Aceptadas:       %d\n", solicitudes_ok);
    printf(" Extemporáneas:   %d\n", solicitudes_extemporaneas);
    printf(" Reprogramadas:   %d\n", solicitudes_reprogramadas);
    printf(" Negadas:         %d\n", solicitudes_negadas);

    int max = -1, min = 9999;
    for (int h = horaIniSim; h <= horaFinSim; h++) {
        if (ocupacion[h] > max) max = ocupacion[h];
        if (ocupacion[h] < min) min = ocupacion[h];
    }

    printf("\nHoras pico (%d): ", max);
    for (int h = horaIniSim; h <= horaFinSim; h++) {
        if (ocupacion[h] == max) printf("%d ", h);
    }

    printf("\nHoras valle (%d): ", min);
    for (int h = horaIniSim; h <= horaFinSim; h++) {
        if (ocupacion[h] == min) {
            printf("%d ", h);
        }
    }
    printf("\n===========================\n");
}

// Programa principal.
int main(int argc, char *argv[]) {
    printf("🚀 Controlador - Iniciando...\n");

    int opcion;
    horaIniSim = horaFinSim = aforoMax = segHorasSim = -1;

    // Procesar argumentos de línea de comandos.
    while ((opcion = getopt(argc, argv, "i:f:s:t:p:")) != -1) {
        switch (opcion) {
        case 'i': 
            horaIniSim = atoi(optarg); 
            break;
        case 'f': 
            horaFinSim = atoi(optarg); 
            break;
        case 's': 
            segHorasSim = atoi(optarg); 
            break;
        case 't': 
            aforoMax   = atoi(optarg); 
            break;
        case 'p': 
            pipe_principal = optarg; 
            break;
        }
    }

    // Validar parámetros obligatorios.
    if (!pipe_principal || horaIniSim < 7 || horaFinSim > 19 ||
    horaIniSim >= horaFinSim || aforoMax <= 0 || segHorasSim <= 0) {
        fprintf(stderr, "Parámetros inválidos.\n");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < MAX_HORAS+2; i++) {
        ocupacion[i] = 0;
    }

    // Crear pipe principal.
    crear_pipe(pipe_principal);
    hora_actual = horaIniSim;

    // Crear hilos de reloj y recepción.
    pthread_t thReloj, thRecv;
    pthread_create(&thReloj, NULL, hiloReloj, NULL);
    pthread_create(&thRecv,  NULL, hiloRecepcion, NULL);

    // Esperar a que termine el hilo de reloj
    pthread_join(thReloj, NULL);
    
    // Activar flag de terminación
    debe_terminar = 1;
    
    // Dar tiempo breve para que el hilo de recepción termine naturalmente
    sleep(1);
    
    // Forzar cancelación del hilo de recepción
    pthread_cancel(thRecv);
    pthread_detach(thRecv);

    reporte_final();

    unlink(pipe_principal);
    return 0;
}
