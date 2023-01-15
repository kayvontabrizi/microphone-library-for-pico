/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 */

#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "OpenPDM2PCM/OpenPDMFilter.h"

#include "pdm_microphone.pio.h"

#include "pico/pdm_microphone.h"

#define PDM_DECIMATION       64
#define PDM_RAW_BUFFER_COUNT 2

static struct {
    struct pdm_microphone_config config;
    int dma_channel;
    uint8_t* raw_buffer[PDM_RAW_BUFFER_COUNT];
    volatile int raw_buffer_write_index;
    volatile int raw_buffer_read_index;
    uint raw_buffer_size;
    uint dma_irq;
    TPDMFilter_InitStruct filters[N_CHANNELS];
    uint16_t filter_volume;
    pdm_samples_ready_handler_t samples_ready_handler;
} pdm_mic;

static void pdm_dma_handler();

int pdm_microphone_init(const struct pdm_microphone_config* config) {
    memset(&pdm_mic, 0x00, sizeof(pdm_mic));
    memcpy(&pdm_mic.config, config, sizeof(pdm_mic.config));

    if (config->sample_buffer_size % (config->sample_rate / 1000)) {
        return -1;
    }

    pdm_mic.raw_buffer_size = config->sample_buffer_size * (PDM_DECIMATION / 8) * N_CHANNELS;

    for (int i = 0; i < PDM_RAW_BUFFER_COUNT; i++) {
        pdm_mic.raw_buffer[i] = malloc(pdm_mic.raw_buffer_size);
        if (pdm_mic.raw_buffer[i] == NULL) {
            pdm_microphone_deinit();

            return -1;   
        }
    }

    pdm_mic.dma_channel = dma_claim_unused_channel(true);
    if (pdm_mic.dma_channel < 0) {
        pdm_microphone_deinit();

        return -1;
    }

#if N_CHANNELS == 1
    uint pio_sm_offset = pio_add_program(config->pio, &pdm_microphone_data_n1_program);
#elif N_CHANNELS == 2
    uint pio_sm_offset = pio_add_program(config->pio, &pdm_microphone_data_n2_program);
#elif N_CHANNELS == 4
    uint pio_sm_offset = pio_add_program(config->pio, &pdm_microphone_data_n4_program);
#else
    #error "Unsupported N_CHANNELS value!"
#endif

    // TODO: PIO INSTRUCTION COUNT IS HARDCODED
    float clk_div = clock_get_hz(clk_sys) / (config->sample_rate * PDM_DECIMATION * 4.0);

    pdm_microphone_data_init(
        config->pio,
        config->pio_sm,
        pio_sm_offset,
        clk_div,
        config->gpio_data,
        config->gpio_clk,
        N_CHANNELS
    );

    dma_channel_config dma_channel_cfg = dma_channel_get_default_config(pdm_mic.dma_channel);

#if N_CHANNELS == 1
    channel_config_set_transfer_data_size(&dma_channel_cfg, DMA_SIZE_8);
#elif N_CHANNELS == 2
    channel_config_set_transfer_data_size(&dma_channel_cfg, DMA_SIZE_16);
#elif N_CHANNELS == 4
    channel_config_set_transfer_data_size(&dma_channel_cfg, DMA_SIZE_32);
#else
    #error "Unsupported N_CHANNELS value!"
#endif
    channel_config_set_read_increment(&dma_channel_cfg, false);
    channel_config_set_write_increment(&dma_channel_cfg, true);
    channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(config->pio, config->pio_sm, false));

    pdm_mic.dma_irq = DMA_IRQ_0;

    dma_channel_configure(
        pdm_mic.dma_channel,
        &dma_channel_cfg,
        pdm_mic.raw_buffer[0],
        &config->pio->rxf[config->pio_sm],
        pdm_mic.raw_buffer_size/N_CHANNELS,
        false
    );

    // TODO: avoid four separate filters
    for (uint i = 0; i < N_CHANNELS; i++) {
        pdm_mic.filters[i].Fs = config->sample_rate;
        pdm_mic.filters[i].LP_HZ = config->sample_rate / 2;
        pdm_mic.filters[i].HP_HZ = 10;
        pdm_mic.filters[i].In_MicChannels = 1;
        pdm_mic.filters[i].Out_MicChannels = 1;
        pdm_mic.filters[i].Decimation = PDM_DECIMATION;
        pdm_mic.filters[i].MaxVolume = 64;
        pdm_mic.filters[i].Gain = 16;
    }

    pdm_mic.filter_volume = pdm_mic.filters[0].MaxVolume;
}

void pdm_microphone_deinit() {
    for (int i = 0; i < PDM_RAW_BUFFER_COUNT; i++) {
        if (pdm_mic.raw_buffer[i]) {
            free(pdm_mic.raw_buffer[i]);

            pdm_mic.raw_buffer[i] = NULL;
        }
    }

    if (pdm_mic.dma_channel > -1) {
        dma_channel_unclaim(pdm_mic.dma_channel);

        pdm_mic.dma_channel = -1;
    }
}

int pdm_microphone_start() {
    irq_set_enabled(pdm_mic.dma_irq, true);
    irq_set_exclusive_handler(pdm_mic.dma_irq, pdm_dma_handler);

    if (pdm_mic.dma_irq == DMA_IRQ_0) {
        dma_channel_set_irq0_enabled(pdm_mic.dma_channel, true);
    } else if (pdm_mic.dma_irq == DMA_IRQ_1) {
        dma_channel_set_irq1_enabled(pdm_mic.dma_channel, true);
    } else {
        return -1;
    }

    // TODO: avoid four separate filters
    for (uint i = 0; i < N_CHANNELS; i++) {
        Open_PDM_Filter_Init(&pdm_mic.filters[i]);
    }

    pio_sm_set_enabled(
        pdm_mic.config.pio,
        pdm_mic.config.pio_sm,
        true
    );

    pdm_mic.raw_buffer_write_index = 0;
    pdm_mic.raw_buffer_read_index = 0;

    dma_channel_transfer_to_buffer_now(
        pdm_mic.dma_channel,
        pdm_mic.raw_buffer[0],
        pdm_mic.raw_buffer_size/N_CHANNELS
    );

    pio_sm_set_enabled(
        pdm_mic.config.pio,
        pdm_mic.config.pio_sm,
        true
    );
}

void pdm_microphone_stop() {
    pio_sm_set_enabled(
        pdm_mic.config.pio,
        pdm_mic.config.pio_sm,
        false
    );

    dma_channel_abort(pdm_mic.dma_channel);

    if (pdm_mic.dma_irq == DMA_IRQ_0) {
        dma_channel_set_irq0_enabled(pdm_mic.dma_channel, false);
    } else if (pdm_mic.dma_irq == DMA_IRQ_1) {
        dma_channel_set_irq1_enabled(pdm_mic.dma_channel, false);
    }

    irq_set_enabled(pdm_mic.dma_irq, false);
}

static void pdm_dma_handler() {
    // clear IRQ
    if (pdm_mic.dma_irq == DMA_IRQ_0) {
        dma_hw->ints0 = (1u << pdm_mic.dma_channel);
    } else if (pdm_mic.dma_irq == DMA_IRQ_1) {
        dma_hw->ints1 = (1u << pdm_mic.dma_channel);
    }

    // get the current buffer index
    pdm_mic.raw_buffer_read_index = pdm_mic.raw_buffer_write_index;

    // get the next capture index to send the dma to start
    pdm_mic.raw_buffer_write_index = (pdm_mic.raw_buffer_write_index + 1) % PDM_RAW_BUFFER_COUNT;

    // give the channel a new buffer to write to and re-trigger it
    dma_channel_transfer_to_buffer_now(
        pdm_mic.dma_channel,
        pdm_mic.raw_buffer[pdm_mic.raw_buffer_write_index],
        pdm_mic.raw_buffer_size/N_CHANNELS
    );

    if (pdm_mic.samples_ready_handler) {
        pdm_mic.samples_ready_handler();
    }
}

void pdm_microphone_set_samples_ready_handler(pdm_samples_ready_handler_t handler) {
    pdm_mic.samples_ready_handler = handler;
}

void pdm_microphone_set_filter_max_volume(uint8_t max_volume) {
    pdm_mic.filters[0].MaxVolume = max_volume;
}

void pdm_microphone_set_filter_gain(uint8_t gain) {
    pdm_mic.filters[0].Gain = gain;
}

void pdm_microphone_set_filter_volume(uint16_t volume) {
    pdm_mic.filter_volume = volume;
}

// morton_even - extract even bits
uint16_t morton_even(uint32_t x)
{
    x = x & 0x55555555;
    x = (x | (x >> 1)) & 0x33333333;
    x = (x | (x >> 2)) & 0x0F0F0F0F;
    x = (x | (x >> 4)) & 0x00FF00FF;
    x = (x | (x >> 8)) & 0x0000FFFF;
    return (uint16_t)x;
}

// morton2 - extract odd and even bits
void morton2(uint16_t *x, uint16_t *y, uint32_t z)
{
    *x = morton_even(z);
    *y = morton_even(z >> 1);
}

// morton_fourth - extract every fourth bit
uint8_t morton_fourth(uint32_t x)
{
    x = x & 0x11111111;
    x = (x | (x >>  3)) & 0x03030303;
    x = (x | (x >>  6)) & 0x000F000F;
    x = (x | (x >> 12)) & 0x000000FF;
    return (uint8_t)x;
}

// morton4 - de-interleave 4 channels
void morton4(uint8_t *a, uint8_t *b, uint8_t *c, uint8_t *d, uint32_t z)
{
    *a = morton_fourth(z);
    *b = morton_fourth(z >> 1);
    *c = morton_fourth(z >> 2);
    *d = morton_fourth(z >> 3);
}

// temporary de-interleaving buffer
#define MAX_SAMPLE_RATE 192000
uint8_t tmp_buffer[N_CHANNELS][(MAX_SAMPLE_RATE/1000) * (PDM_DECIMATION / 8)];

int pdm_microphone_read(int16_t* buffer, size_t raw_n_samples) {
    int filter_stride = (pdm_mic.filters[0].Fs / 1000);
    size_t n_samples = (raw_n_samples / filter_stride) * filter_stride;

    if (n_samples > pdm_mic.config.sample_buffer_size) {
        n_samples = pdm_mic.config.sample_buffer_size;
    }

    if (pdm_mic.raw_buffer_write_index == pdm_mic.raw_buffer_read_index) {
        return 0;
    }

    // de-interleave
    uint32_t* read_raw_buffer = (uint32_t*)pdm_mic.raw_buffer[pdm_mic.raw_buffer_read_index];
    void* edit_tmp_buffers[N_CHANNELS];
    for (int j = 0; j < N_CHANNELS; j++) edit_tmp_buffers[j] = (void*)tmp_buffer[j];
    for (uint i = 0; i < pdm_mic.raw_buffer_size*sizeof(uint8_t)/sizeof(uint32_t); i++) {
#if N_CHANNELS == 1
        *((uint32_t*)edit_tmp_buffers[0] + i) = *(read_raw_buffer + i); // pass through
#elif N_CHANNELS == 2
        morton2(
            (uint16_t*)edit_tmp_buffers[0] + i,
            (uint16_t*)edit_tmp_buffers[1] + i,
            *(read_raw_buffer + i)
        );
#elif N_CHANNELS == 4
        morton4(
            (uint8_t*)edit_tmp_buffers[0] + i,
            (uint8_t*)edit_tmp_buffers[1] + i,
            (uint8_t*)edit_tmp_buffers[2] + i,
            (uint8_t*)edit_tmp_buffers[3] + i,
            *(read_raw_buffer + i)
        );
#else
        #error "Unsupported N_CHANNELS value!"
#endif
    }

    for (int j = 0; j < N_CHANNELS; j++) {
        uint8_t* in = (uint8_t*)tmp_buffer[j];
        int16_t* out = buffer+j*raw_n_samples;

        for (int i = 0; i < n_samples; i += filter_stride) {
#if PDM_DECIMATION == 64
            Open_PDM_Filter_64(in, out, pdm_mic.filter_volume, &pdm_mic.filters[j]);
#elif PDM_DECIMATION == 128
            Open_PDM_Filter_128(in, out, pdm_mic.filter_volume, &pdm_mic.filters[j]);
#else
            #error "Unsupported PDM_DECIMATION value!"
#endif

            in += filter_stride * (PDM_DECIMATION / 8);
            out += filter_stride;
        }
    }

    pdm_mic.raw_buffer_read_index++;

    return n_samples;
}
