#ifndef COMUNES_H
#define COMUNES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

// Funciones comunes
int crear_pipe(const char *nombre);
int abrir_pipe_escritura(const char *nombre);
int abrir_pipe_lectura(const char *nombre);

#endif
