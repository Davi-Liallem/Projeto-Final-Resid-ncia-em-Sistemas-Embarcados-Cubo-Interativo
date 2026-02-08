#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

#include "kiss_fftr.h"
#include "neopixel.h"

#include "mic.h"

// =========================
// Configuração do Microfone
// =========================
#define MIC_CHANNEL      2      // ADC2 -> GPIO 28
#define MIC_PIN          28
#define ADC_CLOCK_DIV    48.f   // ~20kHz
#define SAMPLES          256
#define SAMPLE_RATE      20000

#define NOISE_THRESHOLD  0.9f
#define IGNORE_FREQ_MIN  380.0f
#define IGNORE_FREQ_MAX  400.0f

#define DMA_TIMEOUT_MS   50

// =========================
// LEDs Neopixel
// =========================
#define LED_PIN          7
#define LED_COUNT        25
#define MATRIX_WIDTH     5
#define MATRIX_HEIGHT    5

// =========================
// Globais
// =========================
static uint dma_channel;
static dma_channel_config dma_cfg;

static uint16_t adc_buffer[SAMPLES];
static float    fft_input[SAMPLES];
static kiss_fft_cpx fft_output[SAMPLES / 2];

static kiss_fftr_cfg kiss_cfg = NULL;

// últimos valores (para telemetria)
static volatile float   g_last_freq = 0.0f;
static volatile float   g_last_int  = 0.0f;
static volatile uint8_t g_last_type = 0;

// protótipos internos
static bool sample_mic(void);
static void apply_fft(void);
static uint8_t detect_sound_type(float freq, float intensity);
static void update_leds(uint8_t sound_type);

bool mic_get_last(float *freq_hz, float *intensity, uint8_t *type) {
    if (freq_hz)   *freq_hz   = g_last_freq;
    if (intensity) *intensity = g_last_int;
    if (type)      *type      = g_last_type;
    return true;
}

void mic_init(void)
{
    sleep_ms(500);

    npInit(LED_PIN, LED_COUNT);
    npClear();
    npWrite();

    adc_init();
    adc_gpio_init(MIC_PIN);
    adc_select_input(MIC_CHANNEL);

    adc_fifo_setup(
        true,
        true,
        1,
        false,
        false
    );

    adc_set_clkdiv(ADC_CLOCK_DIV);

    dma_channel = dma_claim_unused_channel(true);
    dma_cfg = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_dreq(&dma_cfg, DREQ_ADC);

    kiss_cfg = kiss_fftr_alloc(SAMPLES, 0, NULL, NULL);

    printf("mic_init: ADC/DMA/FFT/LEDs inicializados.\n");
}

void mic_process(void)
{
    if (!sample_mic()) {
        npClear();
        npWrite();

        static uint32_t last_err_ms = 0;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_err_ms >= 2000) {
            last_err_ms = now_ms;
            printf("[MIC] ERRO: timeout no DMA/ADC (pulando ciclo)\n");
        }
        return;
    }

    apply_fft();

    int max_index = 1;
    float max_mag2 = 0.0f;

    for (int i = 1; i < SAMPLES / 2; i++) {
        float r = fft_output[i].r;
        float im = fft_output[i].i;
        float mag2 = (r * r) + (im * im);
        if (mag2 > max_mag2) {
            max_mag2 = mag2;
            max_index = i;
        }
    }

    float dominant_freq = (max_index * SAMPLE_RATE) / (float)SAMPLES;
    float max_magnitude = sqrtf(max_mag2);
    uint8_t sound_type = detect_sound_type(dominant_freq, max_magnitude);

    // salva para telemetria
    g_last_freq = dominant_freq;
    g_last_int  = max_magnitude;
    g_last_type = sound_type;

    // log controlado
    static uint32_t last_log_ms = 0;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - last_log_ms >= 2000) {
        last_log_ms = now_ms;
        printf("Freq: %.1f Hz | Int: %.3f | tipo=%u\n",
               dominant_freq, max_magnitude, sound_type);
    }

    update_leds(sound_type);
}

static bool sample_mic(void)
{
    adc_fifo_drain();
    adc_run(false);

    dma_channel_configure(
        dma_channel,
        &dma_cfg,
        adc_buffer,
        &adc_hw->fifo,
        SAMPLES,
        true
    );

    adc_run(true);

    absolute_time_t t0 = get_absolute_time();

    while (dma_channel_is_busy(dma_channel)) {
        if (absolute_time_diff_us(t0, get_absolute_time()) > (int64_t)DMA_TIMEOUT_MS * 1000) {
            dma_channel_abort(dma_channel);
            adc_run(false);
            adc_fifo_drain();
            return false;
        }
        tight_loop_contents();
    }

    adc_run(false);
    return true;
}

static void apply_fft(void)
{
    float mean = 0.0f;
    for (int i = 0; i < SAMPLES; i++) mean += (float)adc_buffer[i];
    mean /= (float)SAMPLES;

    for (int i = 0; i < SAMPLES; i++) {
        float v = ((float)adc_buffer[i] - mean);
        fft_input[i] = v / 2048.0f;
    }

    if (kiss_cfg != NULL) {
        kiss_fftr(kiss_cfg, fft_input, fft_output);
    }
}

static uint8_t detect_sound_type(float freq, float intensity)
{
    if (intensity < NOISE_THRESHOLD)
        return 0;

    if (freq > IGNORE_FREQ_MIN && freq < IGNORE_FREQ_MAX)
        return 0;

    if (freq < 200.0f)
        return 1;

    if (freq < 600.0f)
        return 2;

    return 3;
}

static void update_leds(uint8_t sound_type)
{
    npClear();

    uint8_t r = 0, g = 0, b = 0;
    int start_row = 0, end_row = -1;

    if (sound_type == 1) {
        r = 0; g = 80; b = 0;
        start_row = 0;
        end_row   = 0;
    } else if (sound_type == 2) {
        r = 80; g = 80; b = 0;
        start_row = 1;
        end_row   = 2;
    } else if (sound_type == 3) {
        r = 80; g = 0; b = 0;
        start_row = 3;
        end_row   = 4;
    } else {
        npWrite();
        return;
    }

    for (int row = start_row; row <= end_row; row++) {
        for (int col = 0; col < MATRIX_WIDTH; col++) {
            uint index = row * MATRIX_WIDTH + col;
            if (index < LED_COUNT) {
                npSetLED(index, r, g, b);
            }
        }
    }

    npWrite();
}
