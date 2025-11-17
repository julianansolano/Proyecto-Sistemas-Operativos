#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "comunes.h"
#include "../include/estructuras.h"

#define MAX_HORAS 24

// Variables globales sencillas para el caso "reserva OK"
static int horaIniSim   = 7;   // hora mínima de simulación (por defecto)
static int horaFinSim   = 19;  // hora máxima de simulación (por defecto)
static int aforoMax     = 50;  // personas máximas por hora (por defecto)
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
 * Procesa el caso de RESERVA_OK:
 *  - actualiza la ocupación
 *  - arma la respuesta
 *  - la envía por el pipe de respuesta del agente
 */
static void procesar_reserva_ok(const MensajeReserva *msg) {
    // Actualizar ocupación en las dos horas
    ocupacion[msg->hora_solicitada]     += msg->num_personas;
    ocupacion[msg->hora_solicitada + 1] += msg->num_personas;

    // Construir la respuesta
    RespuestaControlador resp;
    resp.tipo = RESERVA_OK;
    resp.hora_asignada = msg->hora_solicitada;
    snprintf(
        resp.mensaje,
        sizeof(resp.mensaje),
        "Reserva OK para familia %s de %d:00 a %d:00",
        msg->nombre_familia,
        msg->hora_solicitada,
        msg->hora_solicitada + 2
    );

    // Abrir el pipe de respuesta del agente
    int fd_resp = abrir_pipe_escritura(msg->pipe_respuesta);
    if (fd_resp == -1) {
        fprintf(stderr, "[CONTROLADOR] No se pudo abrir el pipe de respuesta del agente: %s\n",
                msg->pipe_respuesta);
        return;
    }

    ssize_t escritos = write(fd_resp, &resp, sizeof(resp));
    if (escritos != sizeof(resp)) {
        perror("[CONTROLADOR] Error escribiendo respuesta al agente");
    }
    close(fd_resp);

    // Mensaje en la consola del controlador
    printf("[CONTROLADOR] RESERVA OK -> agente=%s, familia=%s, hora=%d, personas=%d\n",
           msg->nombre_agente,
           msg->nombre_familia,
           msg->hora_solicitada,
           msg->num_personas);
}

int main(int argc, char *argv[]) {
    printf("🚀 Controlador de Reservas - Iniciando (caso RESERVA_OK)...\n");

    if (argc < 5) {
        fprintf(stderr,
                "Uso: %s <pipe_principal> <hora_ini> <hora_fin> <aforo_max>\n"
                "Ejemplo: %s /tmp/pipe_controlador 7 19 50\n",
                argv[0], argv[0]);
     //   return EXIT_FAILURE;
    }

    const char *pipe_principal = argv[1];
    horaIniSim = atoi(argv[2]);
    horaFinSim = atoi(argv[3]);
    aforoMax   = atoi(argv[4]);

    if (horaIniSim < 0 || horaFinSim > MAX_HORAS || horaIniSim >= horaFinSim) {
        fprintf(stderr, "Rango de horas inválido.\n");
        return EXIT_FAILURE;
    }

    if (aforoMax <= 0) {
        fprintf(stderr, "Aforo máximo inválido.\n");
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

    printf("[CONTROLADOR] Parámetros: pipe=%s, horaIni=%d, horaFin=%d, aforoMax=%d\n",
           pipe_principal, horaIniSim, horaFinSim, aforoMax);
    printf("[CONTROLADOR] Esperando solicitudes...\n");

    // Bucle principal: leer mensajes de los agentes
    while (1) {
        int fd = abrir_pipe_lectura(pipe_principal);
        if (fd == -1) {
            // Si falla, intentamos de nuevo
            sleep(1);
            continue;
        }

        MensajeReserva msg;
        ssize_t leidos = read(fd, &msg, sizeof(msg));
        close(fd);

        if (leidos == 0) {
            // Nadie escribió; seguir esperando
            continue;
        }
        if (leidos != sizeof(msg)) {
            fprintf(stderr, "[CONTROLADOR] Tamaño de mensaje inválido (%zd bytes)\n", leidos);
            continue;
        }

        printf("[CONTROLADOR] Solicitud recibida -> agente=%s, familia=%s, hora=%d, personas=%d\n",
               msg.nombre_agente,
               msg.nombre_familia,
               msg.hora_solicitada,
               msg.num_personas);

        // SOLO manejamos el caso de RESERVA_OK
        if (puede_reservar_en_hora(msg.hora_solicitada, msg.num_personas)) {
            procesar_reserva_ok(&msg);
        } else {
            // Aquí luego puedes implementar:
            //  - RESERVA_OTRAS_HORAS
            //  - RESERVA_EXTEMPORANEA
            //  - RESERVA_NEGADA
            printf("[CONTROLADOR] Esta implementación solo maneja reservas OK. "
                   "Solicitud NO atendida.\n");
        }
    }

    return EXIT_SUCCESS;
}
