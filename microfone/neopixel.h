#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <stdint.h>
#include "pico/stdlib.h"

// Inicializa LEDs
void npInit(uint pin, uint led_count);

// Seta cor de um LED
void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b);

// Limpa LEDs
void npClear(void);

// Envia para a fita/matriz
void npWrite(void);

#endif
