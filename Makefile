# Compilador y banderas.
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -Iinclude

# Directorios.
SRC_DIR = src
BIN_DIR = bin
INC_DIR = include

# Ejecutables.
TARGET_CONTROLADOR = $(BIN_DIR)/controlador
TARGET_AGENTE = $(BIN_DIR)/agente

# Archivos fuente.
SRC_CONTROLADOR = $(SRC_DIR)/controlador.c $(SRC_DIR)/comunes.c
SRC_AGENTE = $(SRC_DIR)/agente.c $(SRC_DIR)/comunes.c

# Regla por defecto.
all: $(TARGET_CONTROLADOR) $(TARGET_AGENTE)

# Crear ejecutable del controlador.
$(TARGET_CONTROLADOR): $(SRC_CONTROLADOR)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

# Crear ejecutable del agente.
$(TARGET_AGENTE): $(SRC_AGENTE)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

# Limpiar binarios.
clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
