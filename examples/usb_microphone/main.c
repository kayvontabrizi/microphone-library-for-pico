/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * This examples creates a USB Microphone device using the TinyUSB
 * library and captures data from a PDM microphone using a sample
 * rate of 16 kHz, to be sent the to PC.
 * 
 * The USB microphone code is based on the TinyUSB audio_test example.
 * 
 * https://github.com/hathach/tinyusb/tree/master/examples/device/audio_test
 */

#include "pico/critical_section.h"

#include "pico/pdm_microphone.h"

#include "usb_microphone.h"

// configuration
const struct pdm_microphone_config config = {
  .gpio_clk = 6,
  .gpio_data = 2,
  .pio = pio0,
  .pio_sm = 0,
  .sample_rate = SAMPLE_RATE,
  .sample_buffer_size = SAMPLE_BUFFER_SIZE,
};

// variables
critical_section_t crit_sect;
uint16_t sample_buffer[SAMPLE_BUFFER_SIZE*CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX];

// callback functions
void on_pdm_samples_ready();
void on_usb_microphone_tx_ready();

int main(void)
{
  critical_section_init(&crit_sect);
  // initialize and start the PDM microphone
  pdm_microphone_init(&config);
  pdm_microphone_set_samples_ready_handler(on_pdm_samples_ready);
  pdm_microphone_start();

  // initialize the USB microphone interface
  usb_microphone_init();
  usb_microphone_set_tx_ready_handler(on_usb_microphone_tx_ready);

  while (1) {
    // run the USB microphone task continuously
    usb_microphone_task();
  }

  return 0;
}

void on_pdm_samples_ready()
{
  // Callback from library when all the samples in the library
  // internal sample buffer are ready for reading.
  //
  // Do nothing.
}

void on_usb_microphone_tx_ready()
{
  // Callback from TinyUSB library when all data is ready
  // to be transmitted.
  //
  // Write local buffer to the USB microphone
  critical_section_enter_blocking(&crit_sect);
  pdm_microphone_read(sample_buffer, SAMPLE_BUFFER_SIZE);
  usb_microphone_write(sample_buffer, sizeof(sample_buffer)/CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX);
  critical_section_exit(&crit_sect);
}
