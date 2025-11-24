#include "comunes.h"
#include "../include/estructuras.h"

int crear_pipe(const char *nombre) {
    unlink(nombre);
    
    if (mkfifo(nombre, 0666) == -1) {
        perror("Error creando pipe");
        return -1;
    }
    
    printf("Pipe creado: %s\n", nombre);
    return 0;
}

int abrir_pipe_escritura(const char *nombre) {
    int fd = open(nombre, O_WRONLY);
    if (fd == -1) {
        perror("Error abriendo pipe para escritura");
    }
    return fd;
}

int abrir_pipe_lectura(const char *nombre) {
    int fd = open(nombre, O_RDONLY);
    if (fd == -1) {
        perror("Error abriendo pipe para lectura");
    }
    return fd;
}
