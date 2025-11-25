#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "comunes.h"
#include "../include/estructuras.h"

#include <pthread.h>

#define MAX_HORAS 24

// Variables globales para la simulación
static int horaIniSim   = 7;   // hora mínima de simulación
static int horaFinSim   = 19;  // hora máxima de simulación
static int aforoMax     = 50;  // personas máximas por hora
static int segHorasSim  = 1;   // segundos que dura una "hora" de simulación
static int ocupacion[MAX_HORAS + 2];  // personas por hora

//Pal reloj
static int horaActual;          // hora actual de la simulación
static int simulacion_activa = 1;  // bandera para terminar simulación

// Estructura para manejar reservas a nivel de simulación
typedef struct {
    char familia[MAX_NOMBRE];
    int hora_inicio;
    int hora_fin;   
    int personas;
} ReservaActiva;

// Reservas ya aprobadas pero que aún no han "entrado"
static ReservaActiva reservas_programadas[256];
static int total_reservas_programadas = 0;

// Reservas de familias que están actualmente en el parque
static ReservaActiva reservas_en_parque[256];
static int total_en_parque = 0;

/**
 * Verifica si se puede aceptar la reserva tal cual en la hora solicitada.
 * Regresa 1 si es posible, 0 en caso contrario.
 */
static int puede_reservar_en_hora(int horaSolicitada, int numPersonas) {
    // Debe estar dentro del rango de simulación
    if (horaSolicitada < horaIniSim) {
        return 0;
    }
    // La reserva ocupa 2 horas: horaSolicitada y horaSolicitada + 1
    if (horaSolicitada + 1 > horaFinSim) {
        return 0;
    }

    // Verificar aforo en ambas horas
    if (ocupacion[horaSolicitada] + numPersonas > aforoMax) {
        return 0;
    }
    if (ocupacion[horaSolicitada + 1] + numPersonas > aforoMax) {
        return 0;
    }

    return 1;
}

/**
 * Busca un bloque de 2 horas consecutivas dentro del rango de simulación
 * donde quepa la cantidad de personas indicada.
 * Devuelve la hora de inicio del bloque o -1 si no encuentra nada.
 */
static int buscar_bloque_dos_horas(int numPersonas, int horaInicioBusqueda) {
    if (horaInicioBusqueda < horaIniSim) {
        horaInicioBusqueda = horaIniSim;
    }

    for (int h = horaInicioBusqueda; h <= horaFinSim - 1; ++h) {
        if (ocupacion[h] + numPersonas <= aforoMax &&
            ocupacion[h + 1] + numPersonas <= aforoMax) {
            return h;
        }
    }
    return -1;
}

/**
 * Envía una respuesta al agente usando el pipe indicado en el mensaje.
 */
static void enviar_respuesta(const MensajeReserva *msg,
                             const RespuestaControlador *resp) {
    int fd_resp = abrir_pipe_escritura(msg->pipe_respuesta);
    if (fd_resp == -1) {
        fprintf(stderr,
                "[CONTROLADOR] No se pudo abrir el pipe de respuesta del agente: %s\n",
                msg->pipe_respuesta);
        return;
    }

    ssize_t escritos = write(fd_resp, resp, sizeof(*resp));
    if (escritos != (ssize_t)sizeof(*resp)) {
        perror("[CONTROLADOR] Error escribiendo respuesta al agente");
    }
    close(fd_resp);
}

// registrar una reserva aprobada para que el reloj la maneje
static void registrar_reserva_programada(const MensajeReserva *msg, int hora_asignada) {
    if (total_reservas_programadas >= 256) {
        fprintf(stderr, "[CONTROLADOR] Límite de reservas programadas alcanzado\n");
        return;
    }

    ReservaActiva r;
    memset(&r, 0, sizeof(r));
    strncpy(r.familia, msg->nombre_familia, MAX_NOMBRE - 1);
    r.hora_inicio = hora_asignada;
    r.hora_fin    = hora_asignada + 2;   // siempre 2 horas
    r.personas    = msg->num_personas;

    reservas_programadas[total_reservas_programadas++] = r;
}

// Familias que salen del parque cuando se cumple su hora_fin
static void procesar_salidas(void) {
    for (int i = 0; i < total_en_parque; i++) {
        if (reservas_en_parque[i].hora_fin == horaActual) {
            printf("🚪 Sale familia %s (%d personas) a la hora %d\n",
                   reservas_en_parque[i].familia,
                   reservas_en_parque[i].personas,
                   horaActual);

            // Eliminar del arreglo de "en parque"
            for (int j = i; j < total_en_parque - 1; j++) {
                reservas_en_parque[j] = reservas_en_parque[j + 1];
            }
            total_en_parque--;
            i--; // revisar la posición que se desplazó
        }
    }
}

// Familias que entran al parque cuando llega su hora_inicio
static void procesar_entradas(void) {
    for (int i = 0; i < total_reservas_programadas; i++) {
        if (reservas_programadas[i].hora_inicio == horaActual) {
            printf("🏞️ Entra familia %s (%d personas) a la hora %d\n",
                   reservas_programadas[i].familia,
                   reservas_programadas[i].personas,
                   horaActual);

            // Mover la reserva a la lista de "en parque"
            reservas_en_parque[total_en_parque++] = reservas_programadas[i];

            // Eliminar de reservas_programadas (compactar)
            for (int j = i; j < total_reservas_programadas - 1; j++) {
                reservas_programadas[j] = reservas_programadas[j + 1];
            }
            total_reservas_programadas--;
            i--;
        }
    }
}

//Hilo del reloj que avanza la simulación
static void *hilo_reloj(void *arg) {
    (void)arg;
    
    while (simulacion_activa) {
        sleep(segHorasSim);  // cada segHorasSim segundos = 1 hora simulada

        horaActual++;

        printf("\n⏰ El reloj señala que ha transcurrido una hora, y son las %d hr.\n",
               horaActual);

        // Primero salen los que completan sus 2 horas
        procesar_salidas();

        // Luego entran los que empiezan en esta hora
        procesar_entradas();

        if (horaActual >= horaFinSim) {
            simulacion_activa = 0;
            printf("\n Fin del día. Hora final alcanzada (%d).\n", horaActual);
        }
    }

    return NULL;
}


/**
 * Procesa el caso de RESERVA_OK.
 */
static void procesar_reserva_ok(const MensajeReserva *msg) {
    registrar_reserva_programada(msg, msg->hora_solicitada);

    ocupacion[msg->hora_solicitada]     += msg->num_personas;
    ocupacion[msg->hora_solicitada + 1] += msg->num_personas;

    RespuestaControlador resp;
    resp.tipo = RESERVA_OK;
    resp.hora_asignada = msg->hora_solicitada;

    snprintf(
        resp.mensaje,
        sizeof(resp.mensaje),
        "Reserva OK para %s (%d personas) de %d a %d",
        msg->nombre_familia,
        msg->num_personas,
        msg->hora_solicitada,
        msg->hora_solicitada + 2
    );

    enviar_respuesta(msg, &resp);

    printf("[CONTROLADOR] RESERVA OK -> agente=%s, familia=%s, hora=%d, personas=%d\n",
           msg->nombre_agente,
           msg->nombre_familia,
           msg->hora_solicitada,
           msg->num_personas);
}

/**
 * Procesa el caso de RESERVA_OTRAS_HORAS.
 */
static void procesar_reserva_otras_horas(const MensajeReserva *msg, int nuevaHora) {
    registrar_reserva_programada(msg, nuevaHora);

    ocupacion[nuevaHora]     += msg->num_personas;
    ocupacion[nuevaHora + 1] += msg->num_personas;

    RespuestaControlador resp;
    resp.tipo = RESERVA_OTRAS_HORAS;
    resp.hora_asignada = nuevaHora;

    snprintf(
        resp.mensaje,
        sizeof(resp.mensaje),
        "Sin cupo en %d-%d. %s reprogramada a %d-%d",
        msg->hora_solicitada,
        msg->hora_solicitada + 2,
        msg->nombre_familia,
        nuevaHora,
        nuevaHora + 2
    );

    enviar_respuesta(msg, &resp);

    printf("[CONTROLADOR] RESERVA OTRAS HORAS -> agente=%s, familia=%s, nuevaHora=%d, personas=%d\n",
           msg->nombre_agente,
           msg->nombre_familia,
           nuevaHora,
           msg->num_personas);
}

/**
 * Procesa el caso de RESERVA_EXTEMPORANEA.
 */
static void procesar_reserva_extemporanea(const MensajeReserva *msg, int nuevaHora) {
    registrar_reserva_programada(msg, nuevaHora);
    
    ocupacion[nuevaHora]     += msg->num_personas;
    ocupacion[nuevaHora + 1] += msg->num_personas;

    RespuestaControlador resp;
    resp.tipo = RESERVA_EXTEMPORANEA;
    resp.hora_asignada = nuevaHora;

    snprintf(
        resp.mensaje,
        sizeof(resp.mensaje),
        "Hora %d ya pasó. %s reprogramada a %d-%d",
        msg->hora_solicitada,
        msg->nombre_familia,
        nuevaHora,
        nuevaHora + 2
    );

    enviar_respuesta(msg, &resp);

    printf("[CONTROLADOR] RESERVA EXTEMPORANEA -> agente=%s, familia=%s, nuevaHora=%d, personas=%d\n",
           msg->nombre_agente,
           msg->nombre_familia,
           nuevaHora,
           msg->num_personas);
}

/**
 * Procesa el caso de RESERVA_NEGADA.
 */
static void procesar_reserva_negada(const MensajeReserva *msg, const char *razon) {
    RespuestaControlador resp;
    resp.tipo = RESERVA_NEGADA;
    resp.hora_asignada = -1;

    snprintf(
        resp.mensaje,
        sizeof(resp.mensaje),
        "Reserva negada para %s: %s",
        msg->nombre_familia,
        razon
    );

    enviar_respuesta(msg, &resp);

    printf("[CONTROLADOR] RESERVA NEGADA -> agente=%s, familia=%s, razon=%s\n",
           msg->nombre_agente,
           msg->nombre_familia,
           razon);
}

int main(int argc, char *argv[]) {
    printf("🚀 Controlador de Reservas - Iniciando...\n");

    int opt;
    const char *pipe_principal = NULL;
    horaIniSim  = -1;
    horaFinSim  = -1;
    aforoMax    = -1;
    segHorasSim = -1;
    pthread_t th_reloj;       

    while ((opt = getopt(argc, argv, "i:f:s:t:p:")) != -1) {
        switch (opt) {
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
            aforoMax = atoi(optarg);
            break;
        case 'p':
            pipe_principal = optarg;
            break;
        default:
            fprintf(stderr,
              "Uso: %s -i <hora_ini> -f <hora_fin> -s <segHoras> -t <aforo> -p <pipe_principal>\n",
              argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!pipe_principal || horaIniSim == -1 || horaFinSim == -1 ||
        segHorasSim <= 0 || aforoMax <= 0) {

        fprintf(stderr,
          "Parámetros inválidos.\nUso: %s -i <hora_ini> -f <hora_fin> -s <segHoras> -t <aforo> -p <pipe_principal>\n",
          argv[0]);
        return EXIT_FAILURE;
    }

    // Validar rango de horas 7 - 19 como dice el enunciado
    if (horaIniSim < 7 || horaIniSim > 19 ||
        horaFinSim < 7 || horaFinSim > 19 ||
        horaIniSim >= horaFinSim) {

        fprintf(stderr,
          "Rango de simulación inválido. Debe estar entre 7 y 19, y horaIni < horaFin.\n");
        return EXIT_FAILURE;
    }

    // Inicializar ocupaciones
    for (int h = 0; h <= MAX_HORAS + 1; ++h) {
        ocupacion[h] = 0;
    }

    // Inicializar hora actual de la simulación
    horaActual = horaIniSim;
    simulacion_activa = 1;

    // Crear el pipe principal donde escuchará el controlador
    if (crear_pipe(pipe_principal) == -1) {
        return EXIT_FAILURE;
    }

    printf("[CONTROLADOR] Parámetros: pipe=%s, horaIni=%d, horaFin=%d, aforoMax=%d, segHoras=%d\n",
           pipe_principal, horaIniSim, horaFinSim, aforoMax, segHorasSim);
    printf("[CONTROLADOR] Esperando solicitudes...\n");

     // Crear hilo del reloj que usará segHorasSim
    if (pthread_create(&th_reloj, NULL, hilo_reloj, NULL) != 0) {
        perror("[CONTROLADOR] Error creando hilo del reloj");
        return EXIT_FAILURE;
    }


    // ⚠️ Aquí podrías crear un hilo para el reloj que use segHorasSim.
    // Por ahora solo atendemos solicitudes de los agentes.

    while (simulacion_activa) {
        int fd = abrir_pipe_lectura(pipe_principal);
        if (fd == -1) {
            if (!simulacion_activa) {
                break;
            }
            sleep(1);
            continue;
        }

        MensajeReserva msg;
        ssize_t leidos = read(fd, &msg, sizeof(msg));
        close(fd);

        if (leidos == 0) {
            continue;
        }
        if (leidos != (ssize_t)sizeof(msg)) {
            fprintf(stderr, "[CONTROLADOR] Tamaño de mensaje inválido (%zd bytes)\n", leidos);
            continue;
        }

        printf("[CONTROLADOR] Solicitud -> agente=%s, familia=%s, hora=%d, personas=%d\n",
               msg.nombre_agente,
               msg.nombre_familia,
               msg.hora_solicitada,
               msg.num_personas);

        // 1) Validar que no supere el aforo
        if (msg.num_personas > aforoMax) {
            procesar_reserva_negada(&msg, "Grupo supera el aforo permitido");
            continue;
        }

        // 2) Hora mayor al final de la simulación
        if (msg.hora_solicitada > horaFinSim || msg.hora_solicitada + 1 > horaFinSim) {
            procesar_reserva_negada(&msg, "Hora fuera del rango de simulación");
            continue;
        }

        // 3) Hora extemporánea (anterior al inicio de simulación actual)
        if (msg.hora_solicitada < horaIniSim) {
            int nuevaHora = buscar_bloque_dos_horas(msg.num_personas, horaIniSim);
            if (nuevaHora != -1) {
                procesar_reserva_extemporanea(&msg, nuevaHora);
            } else {
                procesar_reserva_negada(&msg, "No hay cupo para reprogramar (extemporáneo)");
            }
            continue;
        }

        // 4) Intentar reserva en la hora pedida
        if (puede_reservar_en_hora(msg.hora_solicitada, msg.num_personas)) {
            procesar_reserva_ok(&msg);
            continue;
        }

        // 5) Buscar reserva garantizada en otras horas
        int nuevaHora = buscar_bloque_dos_horas(msg.num_personas, horaIniSim);
        if (nuevaHora != -1) {
            procesar_reserva_otras_horas(&msg, nuevaHora);
        } else {
            procesar_reserva_negada(&msg, "Sin cupo en ninguna franja de dos horas");
        }
    }

    pthread_join(th_reloj, NULL);

    printf("Controlador de Reservas termina.\n");
    return EXIT_SUCCESS;
}
