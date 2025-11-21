#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include "comunes.h"
#include "../include/estructuras.h"

/**
 * Uso:
 *   ./bin/agente -s <nombre_agente> -a <fileSolicitud> -p <pipe_principal>
 *
 * Donde fileSolicitud tiene líneas tipo:
 *   Zuluaga,8,10
 *   Dominguez,8,4
 *   Rojas,10,10
 */

int main(int argc, char *argv[]) {
    printf("👤 Agente de Reservas - Iniciando...\n");

    char nombre_agente[MAX_NOMBRE] = "";
    char archivo_solicitudes[256] = "";
    const char *pipe_principal = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
        case 's':
            strncpy(nombre_agente, optarg, sizeof(nombre_agente) - 1);
            break;
        case 'a':
            strncpy(archivo_solicitudes, optarg, sizeof(archivo_solicitudes) - 1);
            break;
        case 'p':
            pipe_principal = optarg;
            break;
        default:
            fprintf(stderr,
                    "Uso: %s -s <nombre_agente> -a <fileSolicitud> -p <pipe_principal>\n",
                    argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (nombre_agente[0] == '\0' || archivo_solicitudes[0] == '\0' || pipe_principal == NULL) {
        fprintf(stderr,
                "Parámetros inválidos.\nUso: %s -s <nombre_agente> -a <fileSolicitud> -p <pipe_principal>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    // Abrir archivo de solicitudes
    FILE *f = fopen(archivo_solicitudes, "r");
    if (!f) {
        perror("[AGENTE] No se pudo abrir el archivo de solicitudes");
        return EXIT_FAILURE;
    }

    char linea[256];
    int linea_num = 0;

    while (fgets(linea, sizeof(linea), f)) {
        linea_num++;

        // Saltar líneas vacías
        if (linea[0] == '\n' || linea[0] == '\0')
            continue;

        char nombre_familia[MAX_NOMBRE];
        int hora, personas;

        // Formato: Familia,hora,personas
        if (sscanf(linea, " %63[^,] , %d , %d", nombre_familia, &hora, &personas) != 3) {
            fprintf(stderr, "[AGENTE] Línea %d inválida en %s: %s",
                    linea_num, archivo_solicitudes, linea);
            continue;
        }

        // Construcción del nombre de pipe de respuesta
        char pipe_respuesta[MAX_PIPE_NAME];
        pid_t pid = getpid();
        snprintf(pipe_respuesta, sizeof(pipe_respuesta),
                 "/tmp/pipe_resp_%s_%d", nombre_agente, (int)pid);

        if (crear_pipe(pipe_respuesta) == -1) {
            fclose(f);
            return EXIT_FAILURE;
        }

        // Construir mensaje
        MensajeReserva msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.nombre_agente,  nombre_agente,  MAX_NOMBRE - 1);
        strncpy(msg.nombre_familia, nombre_familia, MAX_NOMBRE - 1);
        msg.hora_solicitada = hora;
        msg.num_personas    = personas;
        strncpy(msg.pipe_respuesta, pipe_respuesta, MAX_PIPE_NAME - 1);

        // Enviar mensaje al controlador
        int fd_envio = abrir_pipe_escritura(pipe_principal);
        if (fd_envio == -1) {
            fprintf(stderr, "[AGENTE] No se pudo abrir el pipe principal %s\n", pipe_principal);
            unlink(pipe_respuesta);
            fclose(f);
            return EXIT_FAILURE;
        }

        ssize_t escritos = write(fd_envio, &msg, sizeof(msg));
        close(fd_envio);

        if (escritos != (ssize_t)sizeof(msg)) {
            perror("[AGENTE] Error enviando mensaje al controlador");
            unlink(pipe_respuesta);
            fclose(f);
            return EXIT_FAILURE;
        }

        printf("[AGENTE:%s] Solicitud enviada -> familia=%s, hora=%d, personas=%d\n",
               nombre_agente, nombre_familia, hora, personas);

        // Esperar respuesta
        int fd_resp = abrir_pipe_lectura(pipe_respuesta);
        if (fd_resp == -1) {
            fprintf(stderr, "[AGENTE] No se pudo abrir el pipe de respuesta %s\n", pipe_respuesta);
            unlink(pipe_respuesta);
            fclose(f);
            return EXIT_FAILURE;
        }

        RespuestaControlador resp;
        ssize_t leidos = read(fd_resp, &resp, sizeof(resp));
        close(fd_resp);
        unlink(pipe_respuesta);

        if (leidos != (ssize_t)sizeof(resp)) {
            fprintf(stderr, "[AGENTE] Tamaño de respuesta inválido (%zd bytes)\n", leidos);
            fclose(f);
            return EXIT_FAILURE;
        }

        // Mostrar respuesta según el tipo
        switch (resp.tipo) {
        case RESERVA_OK:
            printf("[AGENTE:%s] ✅ %s (hora=%d)\n",
                   nombre_agente, resp.mensaje, resp.hora_asignada);
            break;
        case RESERVA_OTRAS_HORAS:
            printf("[AGENTE:%s] 🔁 %s (nueva hora=%d)\n",
                   nombre_agente, resp.mensaje, resp.hora_asignada);
            break;
        case RESERVA_EXTEMPORANEA:
            printf("[AGENTE:%s] ⏰ %s (nueva hora=%d)\n",
                   nombre_agente, resp.mensaje, resp.hora_asignada);
            break;
        case RESERVA_NEGADA:
            printf("[AGENTE:%s] ❌ %s\n", nombre_agente, resp.mensaje);
            break;
        default:
            printf("[AGENTE:%s] Respuesta desconocida: %s (tipo=%d, hora=%d)\n",
                   nombre_agente, resp.mensaje, resp.tipo, resp.hora_asignada);
            break;
        }

        // Paso 5 del enunciado: esperar 2 segundos antes de enviar la siguiente
        sleep(2);
    }

    fclose(f);
    printf("Agente %s termina.\n", nombre_agente);
    return EXIT_SUCCESS;
}
