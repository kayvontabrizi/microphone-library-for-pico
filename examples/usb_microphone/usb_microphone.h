/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 */

#ifndef _USB_MICROPHONE_H_
#define _USB_MICROPHONE_H_

#include "tusb.h"

#define SAMPLE_RATE SAMPLES_PER_MS * 1000
#define SAMPLE_BUFFER_SIZE SAMPLES_PER_MS * MS_PER_FRAME

typedef void (*usb_microphone_tx_ready_handler_t)(void);
typedef void (*usb_microphone_tx_done_handler_t)(void);

void usb_microphone_init();
void usb_microphone_set_tx_ready_handler(usb_microphone_tx_ready_handler_t handler);
void usb_microphone_set_tx_done_handler(usb_microphone_tx_done_handler_t handler);
void usb_microphone_task();
uint16_t usb_microphone_write(const void * data);

#endif
