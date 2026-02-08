#ifndef MIC_H
#define MIC_H

#include <stdbool.h>
#include <stdint.h>

void mic_init(void);
void mic_process(void);

// últimos valores (para telemetria)
bool  mic_get_last(float *freq_hz, float *intensity, uint8_t *type);

#endif
