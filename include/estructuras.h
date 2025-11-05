#ifndef ESTRUCTURAS_H
#define ESTRUCTURAS_H

#define MAX_NOMBRE 50
#define MAX_PIPE_NAME 100

// Estructura para mensajes entre procesos
typedef struct {
    char nombre_agente[MAX_NOMBRE];
    char nombre_familia[MAX_NOMBRE];
    int hora_solicitada;
    int num_personas;
    char pipe_respuesta[MAX_PIPE_NAME];
} MensajeReserva;

// Respuestas del controlador
typedef enum {
    RESERVA_OK,
    RESERVA_OTRAS_HORAS,
    RESERVA_EXTEMPORANEA,
    RESERVA_NEGADA
} TipoRespuesta;

typedef struct {
    TipoRespuesta tipo;
    int hora_asignada;
    char mensaje[100];
} RespuestaControlador;

#endif
