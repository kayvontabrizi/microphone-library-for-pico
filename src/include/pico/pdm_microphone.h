/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 */

#ifndef _PICO_PDM_MICROPHONE_H_
#define _PICO_PDM_MICROPHONE_H_

#include "hardware/pio.h"

#define USB_IS_SLOWER true // this seems to be the preference, but if unsure, leave undefined!
#define N_CHANNELS 1 // # of channels to process (will register as 4-channel device regardless)
#define PDM_DECIMATION       64 // # of PDM samples per PCM samples
#define PDM_RAW_BUFFER_COUNT 64 // # of buffer sections (> 16 to avoid frequent pops)

typedef void (*pdm_samples_ready_handler_t)(void);

struct pdm_microphone_config {
    uint gpio_data;
    uint gpio_clk;
    PIO pio;
    uint pio_sm;
    uint sample_rate;
    uint sample_buffer_size;
};

static void pdm_dma_handler();

int pdm_microphone_init(const struct pdm_microphone_config* config);
void pdm_microphone_deinit();

int pdm_microphone_start();
void pdm_microphone_stop();

void pdm_microphone_set_samples_ready_handler(pdm_samples_ready_handler_t handler);
void pdm_microphone_set_filter_max_volume(uint8_t max_volume);
void pdm_microphone_set_filter_gain(uint8_t gain);
void pdm_microphone_set_filter_volume(uint16_t volume);

int pdm_microphone_read(int16_t* buffer, size_t n_samples);

#endif
