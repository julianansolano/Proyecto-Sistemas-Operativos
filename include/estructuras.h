#ifndef ESTRUCTURAS_H
#define ESTRUCTURAS_H

#define MAX_NOMBRE 50
#define MAX_PIPE_NAME 100

// Tipos de mensajes generales.
typedef enum {
    MSG_HOLA,
    MSG_RESERVA
} TipoMensaje;

// Mensaje de saludo inicial del agente al controlador.
typedef struct {
    TipoMensaje tipo;
    char nombre_agente[MAX_NOMBRE];
    char pipe_respuesta[MAX_PIPE_NAME];
} MensajeHola;

// Estructura para mensajes entre procesos.
typedef struct {
    TipoMensaje tipo;
    char nombre_agente[MAX_NOMBRE];
    char nombre_familia[MAX_NOMBRE];
    char pipe_respuesta[MAX_PIPE_NAME];
    int hora_solicitada;
    int num_personas;
} MensajeReserva;

// Mensaje de bienvenida del controlador al agente.
typedef struct {
    int hora_actual;
} MensajeWelcome;

// Respuestas del controlador.
typedef enum {
    RESERVA_OK,
    RESERVA_OTRAS_HORAS,
    RESERVA_EXTEMPORANEA,
    RESERVA_NEGADA
} TipoRespuesta;

// Estructura para respuestas del controlador.
typedef struct {
    TipoRespuesta tipo;
    int hora_asignada;
    char mensaje[100];
} RespuestaControlador;

#endif
