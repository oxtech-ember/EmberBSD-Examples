/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef NATIVE_USB_H
#define NATIVE_USB_H
#include <stdint.h>
#include <dev/usb/usbperiph.h>
int rx_usb_config(struct usbperiph_config *, uint16_t, uint16_t);
int rx_usb_setup(const uint8_t[8], unsigned *, unsigned *);
#endif
