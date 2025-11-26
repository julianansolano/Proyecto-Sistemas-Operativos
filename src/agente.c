/**
 *  @file agente.c
 *  @brief Agente cliente del sistema de reservas.
 *  
 *  Este  programa representa a un agente  que envía solicitudes de  reserva  al
 *  controlador principal mediante un FIFO (pipe con nombre). Cada agente lee un
 *  archivo con solicitudes, envía mensajes al controlador y espera la respuesta
 *  correspondiente por un pipe propio.
 *  
 *      Flujo de comunicación:
 *  - **HELLO → (pipe principal)**: enviado al iniciar el agente.
 *  - **WELCOME ← (pipe respuesta del agente)**: recibido al iniciar la simulación.
 *  - **RESERVA → (pipe principal)**: por cada línea válida del archivo.
 *  - **RESPUESTA ← (pipe respuesta del agente)**: por cada reserva enviada.
 *  
 *      Parámetros esperados:
 *   -s <nombreAgente> Nombre único del agente.
 *   -a <archivoSolicitudes> Archivo con solicitudes (ej. "Familia,Hora,Personas").
 *   -p <pipePrincipal> FIFO por el cual el controlador recibe mensajes.
 *  
 *  Ejemplo de formato de solicitudes:
 *     Zuluaga,8,10
 *     Dominguez,8,4
 *     Rojas,10,10
 *  
 *  Este módulo no administra ocupación ni aforo; su rol es exclusivamente
 *  comunicarse con el controlador, reenviar solicitudes y esperar respuestas.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include "comunes.h"
#include "../include/estructuras.h"

// Función principal del agente de reservas.
int main(int argc, char *argv[]) {
    // Mensaje de bienvenida.
    printf("👤 Agente de Reservas - Iniciando...\n");
    char nombre_agente[MAX_NOMBRE] = "";
    char archivo_solicitudes[256] = "";
    const char *pipe_principal = NULL;

    // Procesar argumentos.
    int opcion;
    while ((opcion = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opcion) {
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
            fprintf(stderr,"Uso: %s -s <nombre_agente> -a <fileSolicitud> -p <pipe_principal>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Validar parámetros obligatorios.
    if (nombre_agente[0] == '\0' || archivo_solicitudes[0] == '\0' || pipe_principal == NULL) {
        fprintf(stderr,"Parámetros inválidos.\nUso: %s -s <nombre_agente> -a <fileSolicitud> -p <pipe_principal>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Crear pipe de respuesta.
    char pipe_respuesta[MAX_PIPE_NAME];
    snprintf(pipe_respuesta, sizeof(pipe_respuesta), "/tmp/pipe_resp_%s_%d", nombre_agente, getpid());

    if (crear_pipe(pipe_respuesta) == -1) {
        fprintf(stderr, "[AGENTE] No se pudo crear pipe de respuesta %s\n", pipe_respuesta);
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }

    // Abrir pipe principal para envío.
    int fd_envio = abrir_pipe_escritura(pipe_principal);

    if (fd_envio == -1) {
        fprintf(stderr, "[AGENTE] No se pudo abrir pipe principal %s\n", pipe_principal);
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }

    // Enviar mensaje HELLO al controlador.
    MensajeHola hola;
    memset(&hola, 0, sizeof(hola));
    hola.tipo = MSG_HOLA;

    strncpy(hola.nombre_agente, nombre_agente, MAX_NOMBRE - 1);
    strncpy(hola.pipe_respuesta, pipe_respuesta, MAX_PIPE_NAME - 1);

    if (write(fd_envio, &hola, sizeof(hola)) != sizeof(hola)) {
        perror("[AGENTE] Error enviando HELLO");
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }

    printf("[AGENTE:%s] HELLO enviado. Esperando WELCOME...\n", nombre_agente);

    // Esperar mensaje WELCOME del controlador.
    int fd_resp = abrir_pipe_lectura(pipe_respuesta);
    if (fd_resp == -1) {
        fprintf(stderr, "[AGENTE] No se pudo abrir pipe de respuesta %s\n", pipe_respuesta);
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }

    MensajeWelcome welcome;
    ssize_t leidos = read(fd_resp, &welcome, sizeof(welcome));
    close(fd_resp);

    if (leidos != sizeof(welcome)) {
        fprintf(stderr, "[AGENTE] Error leyendo WELCOME\n");
        unlink(pipe_respuesta);
        return EXIT_FAILURE;
    }

    // Mostrar hora actual recibida.
    int horaActual = welcome.hora_actual;
    printf("[AGENTE:%s] WELCOME recibido. Hora actual = %d\n", nombre_agente, horaActual);

    // Abrir archivo de solicitudes.
    FILE *file = fopen(archivo_solicitudes, "r");
    if (!file) {
        perror("[AGENTE] No se pudo abrir el archivo de solicitudes");
        return EXIT_FAILURE;
    }

    char linea[256];
    int linea_num = 0;

    // Procesar cada línea del archivo de solicitudes.
    while (fgets(linea, sizeof(linea), file)) {
        linea_num++;

        // Saltar líneas vacías.
        if (linea[0] == '\n' || linea[0] == '\0') { 
            continue;
        }

        char nombre_familia[MAX_NOMBRE];
        int hora, personas;

        // Formato esperado: NombreFamilia, Hora, Personas.
        if (sscanf(linea, " %63[^,] , %d , %d", nombre_familia, &hora, &personas) != 3) {
            fprintf(stderr, "[AGENTE] Línea %d inválida en %s: %s", linea_num, archivo_solicitudes, linea);
            continue;
        }

        // Validación de la hora.
        if (hora < horaActual) {
            printf("[AGENTE:%s] Solicitud ignorada (extemporánea): familia=%s, hora=%d\n", nombre_agente, nombre_familia, hora);
            continue;
        }

        // Crear y enviar mensaje de reserva.
        MensajeReserva msg;
        memset(&msg, 0, sizeof(msg));
        msg.tipo = MSG_RESERVA;

        strncpy(msg.nombre_agente, nombre_agente,  MAX_NOMBRE - 1);
        strncpy(msg.nombre_familia, nombre_familia, MAX_NOMBRE - 1);
        strncpy(msg.pipe_respuesta, pipe_respuesta, MAX_PIPE_NAME - 1);

        msg.hora_solicitada = hora;
        msg.num_personas = personas;

            // Esperar 2 segundos antes de enviar la siguiente, según enunciado.
            sleep(2);
        // Enviar mensaje de reserva al controlador.
        if (write(fd_envio, &msg, sizeof(msg)) != sizeof(msg)) {
            perror("[AGENTE] Error enviando mensaje");
            fclose(file);
            unlink(pipe_respuesta);
            return EXIT_FAILURE;
        }

        printf("[AGENTE:%s] Solicitud enviada -> familia=%s, hora=%d, personas=%d\n",
                nombre_agente, nombre_familia, hora, personas);

        // Esperar respuesta del controlador.
        fd_resp = abrir_pipe_lectura(pipe_respuesta);
        if (fd_resp == -1) {
            fprintf(stderr, "[AGENTE] Error abriendo pipe respuesta %s\n", pipe_respuesta);
            fclose(file);
            unlink(pipe_respuesta);
            return EXIT_FAILURE;
        }

        RespuestaControlador respuesta;
        leidos = read(fd_resp, &respuesta, sizeof(respuesta));
        close(fd_resp);

        if (leidos != sizeof(respuesta)) {
            fprintf(stderr, "[AGENTE] Tamaño de respuesta inválido (%zd bytes)\n", leidos);
            fclose(file);
            unlink(pipe_respuesta);
            return EXIT_FAILURE;
        }

        // Mostrar respuesta según el tipo.
        switch (respuesta.tipo) {
        case RESERVA_OK:
            printf("[AGENTE:%s] ✅ %s (hora=%d)\n",
                    nombre_agente, respuesta.mensaje, respuesta.hora_asignada);
            break;
        case RESERVA_OTRAS_HORAS:
            printf("[AGENTE:%s] 🔁 %s (nueva hora=%d)\n",
                    nombre_agente, respuesta.mensaje, respuesta.hora_asignada);
            break;
        case RESERVA_EXTEMPORANEA:
            printf("[AGENTE:%s] ⏰ %s (nueva hora=%d)\n",
                    nombre_agente, respuesta.mensaje, respuesta.hora_asignada);
            break;
        case RESERVA_NEGADA:
            printf("[AGENTE:%s] ❌ %s\n", nombre_agente, respuesta.mensaje);
            break;
        default:
            printf("[AGENTE:%s] Respuesta desconocida: %s (tipo=%d, hora=%d)\n",
                    nombre_agente, respuesta.mensaje, respuesta.tipo, respuesta.hora_asignada);
            break;
        }
    }

    // Cerrar archivo de solicitudes.
    fclose(file);
    printf("Agente %s termina.\n", nombre_agente);

    /* Limpieza: eliminar pipe de respuesta creado por el agente. */
    unlink(pipe_respuesta);

    return EXIT_SUCCESS;
}
