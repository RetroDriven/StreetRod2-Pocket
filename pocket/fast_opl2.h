/* RetroDriven Pocket: lightweight 9-channel YM3812 register interpreter.
 * Experimental FM approximation, not a cycle-accurate AdLib replacement.
 * Copyright 2026 RetroDriven project contributors. MIT license. */
#ifndef RD_FAST_OPL2_H
#define RD_FAST_OPL2_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint8_t regs[256];
    uint32_t phase[18], step[18];
    uint16_t envelope[18];
    uint8_t state[18];
    int16_t prev_mod[9];
    uint32_t noise;
    /* FX-only: bound the white-noise fallback for drums to a short onset. */
    uint16_t fx_noise_remaining;
    bool fx_mode;
    uint32_t writes, keys;
    bool enabled;
} rd_opl2_t;
void rd_opl2_reset(rd_opl2_t *s);
void rd_opl2_enable(rd_opl2_t *s, bool on);
void rd_opl2_fx_mode(rd_opl2_t *s, bool on);
void rd_opl2_write(rd_opl2_t *s, uint8_t reg, uint8_t value);
int16_t rd_opl2_sample(rd_opl2_t *s);
#endif
