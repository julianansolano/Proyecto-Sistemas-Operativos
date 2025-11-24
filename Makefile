CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread
TARGET_CONTROLADOR = bin/controlador
TARGET_AGENTE = bin/agente

all: $(TARGET_CONTROLADOR) $(TARGET_AGENTE)

$(TARGET_CONTROLADOR): src/controlador.c src/comunes.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -Iinclude -o $@ $^

$(TARGET_AGENTE): src/agente.c src/comunes.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -Iinclude -o $@ $^

clean:
	rm -rf bin/

.PHONY: all clean
