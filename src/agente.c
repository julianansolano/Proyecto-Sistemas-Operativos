#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "comunes.h"
#include "../include/estructuras.h"

/**
 * Uso:
 *   agente <pipe_principal> <nombre_agente> <nombre_familia> <hora_solicitada> <num_personas>
 *
 * Ejemplo:
 *   agente /tmp/pipe_controlador Agente1 Perez 8 4
 */
int main(int argc, char *argv[]) {
    printf("👤 Agente de Reservas - Iniciando (caso RESERVA_OK)...\n");

    if (argc < 6) {
        fprintf(stderr,
                "Uso: %s <pipe_principal> <nombre_agente> <nombre_familia> <hora_solicitada> <num_personas>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *pipe_principal = argv[1];
    const char *nombre_agente  = argv[2];
    const char *nombre_familia = argv[3];
    int hora_solicitada        = atoi(argv[4]);
    int num_personas           = atoi(argv[5]);

    // Construimos el nombre del pipe de respuesta usando el PID del proceso
    char pipe_respuesta[MAX_PIPE_NAME];
    pid_t pid = getpid();
    snprintf(pipe_respuesta, sizeof(pipe_respuesta),
             "/tmp/pipe_respuesta_%d", (int)pid);

    // Crear pipe de respuesta para que el controlador pueda contestar
    if (crear_pipe(pipe_respuesta) == -1) {
        return EXIT_FAILURE;
    }

    // Construcción del mensaje
    MensajeReserva msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.nombre_agente,  nombre_agente,  MAX_NOMBRE - 1);
    strncpy(msg.nombre_familia, nombre_familia, MAX_NOMBRE - 1);
    msg.hora_solicitada = hora_solicitada;
    msg.num_personas    = num_personas;
    strncpy(msg.pipe_respuesta, pipe_respuesta, MAX_PIPE_NAME - 1);

    // Abrir el pipe principal para enviar la solicitud al controlador
    int fd_envio = abrir_pipe_escritura(pipe_principal);
    if (fd_envio == -1) {
        fprintf(stderr, "[AGENTE] No se pudo abrir el pipe principal %s\n", pipe_principal);
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }

    ssize_t escritos = write(fd_envio, &msg, sizeof(msg));
    if (escritos != sizeof(msg)) {
        perror("[AGENTE] Error enviando mensaje al controlador");
        close(fd_envio);
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }
    close(fd_envio);

    printf("[AGENTE] Solicitud enviada -> familia=%s, hora=%d, personas=%d\n",
           msg.nombre_familia, msg.hora_solicitada, msg.num_personas);

    // Esperar la respuesta en nuestro pipe de respuesta
    int fd_resp = abrir_pipe_lectura(pipe_respuesta);
    if (fd_resp == -1) {
        fprintf(stderr, "[AGENTE] No se pudo abrir el pipe de respuesta %s\n", pipe_respuesta);
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }

    RespuestaControlador resp;
    ssize_t leidos = read(fd_resp, &resp, sizeof(resp));
    close(fd_resp);
    unlink(pipe_respuesta);  // ya no lo necesitamos

    if (leidos != sizeof(resp)) {
        fprintf(stderr, "[AGENTE] Tamaño de respuesta inválido (%zd bytes)\n", leidos);
        return EXIT_FAILURE;
    }

    // Mostrar la respuesta
    if (resp.tipo == RESERVA_OK) {
        printf("[AGENTE] ✅ %s (hora asignada: %d)\n",
               resp.mensaje, resp.hora_asignada);
    } else {
        // Para este ejercicio solo esperamos RESERVA_OK,
        // pero dejamos este bloque para ampliaciones futuras.
        printf("[AGENTE] Respuesta del controlador: %s (tipo=%d, hora=%d)\n",
               resp.mensaje, resp.tipo, resp.hora_asignada);
    }

    printf("👤 Agente terminado.\n");
    return EXIT_SUCCESS;
}
