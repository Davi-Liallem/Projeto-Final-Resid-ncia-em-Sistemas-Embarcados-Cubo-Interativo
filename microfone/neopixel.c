#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "ws2818b.pio.h"
#include "neopixel.h"

// ----------------------------------------------------------------------
// VARIÁVEIS GLOBAIS
// ----------------------------------------------------------------------
static PIO pio = pio0;   // usamos PIO 0
static uint sm = 0;      // state machine
static uint offset;      // offset do programa PIO
static uint total_leds = 0;

typedef struct {
    uint8_t r, g, b;
} led_t;

static led_t *led_buffer = NULL;

// ----------------------------------------------------------------------
// FUNÇÃO: npInit()
// Inicializa a fita/matriz Neopixel
// ----------------------------------------------------------------------
void npInit(uint pin, uint led_count)
{
    total_leds = led_count;

    // Buffer para armazenar as cores
    led_buffer = (led_t *)calloc(led_count, sizeof(led_t));

    // Carrega o programa PIO
    offset = pio_add_program(pio, &ws2818b_program);

    // ATENÇÃO: Nova assinatura exige freq (800kHz)
    ws2818b_program_init(pio, sm, offset, pin, 800000.0f);

    npClear();
    npWrite();
}

// ----------------------------------------------------------------------
// FUNÇÃO: npSetLED()
// Define a cor de um LED específico
// ----------------------------------------------------------------------
void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= total_leds) return;
    led_buffer[index].r = r;
    led_buffer[index].g = g;
    led_buffer[index].b = b;
}

// ----------------------------------------------------------------------
// FUNÇÃO: npClear()
// Apaga todos os LEDs
// ----------------------------------------------------------------------
void npClear(void)
{
    for (uint i = 0; i < total_leds; i++) {
        led_buffer[i].r = 0;
        led_buffer[i].g = 0;
        led_buffer[i].b = 0;
    }
}

// ----------------------------------------------------------------------
// FUNÇÃO: npWrite()
// Envia os dados via PIO
// ----------------------------------------------------------------------
void npWrite(void)
{
    for (uint i = 0; i < total_leds; i++) {
        // Ordem: GRB (padrão WS2812/WS2818)
        uint32_t grb =
            ((uint32_t)led_buffer[i].g << 16) |
            ((uint32_t)led_buffer[i].r <<  8) |
            ((uint32_t)led_buffer[i].b);

        pio_sm_put_blocking(pio, sm, grb << 8u); 
    }
}
