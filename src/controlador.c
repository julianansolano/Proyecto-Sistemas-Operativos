#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "comunes.h"
#include "../include/estructuras.h"

int main(int argc, char *argv[]) {
    printf("🚀 Controlador de Reservas - Iniciando...\n");
    
    // Por ahora solo imprime los argumentos
    printf("Argumentos recibidos:\n");
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    
    printf("✅ Controlador terminado\n");
    return 0;
}
