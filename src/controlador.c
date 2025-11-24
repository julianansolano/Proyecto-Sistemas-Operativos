#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "comunes.h"
#include "../include/estructuras.h"

#define MAX_HORAS 24

// Variables globales para la simulación
static int horaIniSim   = 7;   // hora mínima de simulación
static int horaFinSim   = 19;  // hora máxima de simulación
static int aforoMax     = 50;  // personas máximas por hora
static int segHorasSim  = 1;   // segundos que dura una "hora" de simulación
static int ocupacion[MAX_HORAS + 2];  // personas por hora

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
        // Verificar que ambas horas estén dentro del rango de simulación
        if (h >= horaIniSim && (h + 1) <= horaFinSim) {
            if (ocupacion[h] + numPersonas <= aforoMax &&
                ocupacion[h + 1] + numPersonas <= aforoMax) {
                return h;
            }
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

/**
 * Procesa el caso de RESERVA_OK.
 */
static void procesar_reserva_ok(const MensajeReserva *msg) {
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

/**
 * Verifica si debe aplicarse "Reserva negada, debe volver otro día"
 * según los criterios iv.a, iv.b, iv.c
 */

static int debe_volver_otro_dia(const MensajeReserva *msg, int hora_actual) {
    // iv.c: El número de personas es mayor al aforo permitido
    if (msg->num_personas > aforoMax) {
        return 1;
    }  // iv.a: La hora solicitada sea mayor a horaFin
     if (msg->hora_solicitada > horaFinSim) {
        return 1;
    }
// iv.b: No encuentra disponible ningún bloque de 2 horas dentro del periodo
    int bloque_encontrado = buscar_bloque_dos_horas(msg->num_personas, hora_actual);
    if (bloque_encontrado == -1) {
        return 1;
    }
    
    return 0;
}

/**
 * Procesa específicamente el caso "Reserva negada, debe volver otro día"
 */
static void procesar_reserva_volver_otro_dia(const MensajeReserva *msg, const char *razon_especifica) {
    RespuestaControlador resp;
    resp.tipo = RESERVA_NEGADA;
    resp.hora_asignada = -1;
    
    snprintf(
        resp.mensaje,
        sizeof(resp.mensaje),
        "Reserva negada para %s. Debe volver otro día. Razón: %s",
        msg->nombre_familia,
        razon_especifica
    );
    
    enviar_respuesta(msg, &resp);
    
    printf("[CONTROLADOR] RESERVA VOLVER OTRO DÍA -> agente=%s, familia=%s, hora_solicitada=%d, personas=%d, razón=%s\n",
           msg->nombre_agente,
           msg->nombre_familia,
           msg->hora_solicitada,
           msg->num_personas,
           razon_especifica);
}

int main(int argc, char *argv[]) {
    printf("🚀 Controlador de Reservas - Iniciando...\n");

    int opt;
    const char *pipe_principal = NULL;
    horaIniSim  = -1;
    horaFinSim  = -1;
    aforoMax    = -1;
    segHorasSim = -1;


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

    // Crear el pipe principal donde escuchará el controlador
    if (crear_pipe(pipe_principal) == -1) {
        return EXIT_FAILURE;
    }

    printf("[CONTROLADOR] Parámetros: pipe=%s, horaIni=%d, horaFin=%d, aforoMax=%d, segHoras=%d\n",
           pipe_principal, horaIniSim, horaFinSim, aforoMax, segHorasSim);
    printf("[CONTROLADOR] Esperando solicitudes...\n");

    // ⚠️ Aquí podrías crear un hilo para el reloj que use segHorasSim.
    // Por ahora solo atendemos solicitudes de los agentes.

    while (1) {
        int fd = abrir_pipe_lectura(pipe_principal);
        if (fd == -1) {
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

        // 1) Verificar si aplica "Volver otro día" (condiciones iv.a, iv.b, iv.c)
        int hora_actual = horaIniSim;
        if (debe_volver_otro_dia(&msg, hora_actual)) {
            // Determinar la razón específica
            const char *razon = "";
            if (msg.num_personas > aforoMax) {
                razon = "Grupo supera el aforo permitido";
            } else if (msg.hora_solicitada > horaFinSim) {
                razon = "Hora solicitada fuera del rango de simulación";
            } else {
                razon = "No hay cupo disponible en ningún bloque de 2 horas";
            }
            
            procesar_reserva_volver_otro_dia(&msg, razon);
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

    return EXIT_SUCCESS;
}