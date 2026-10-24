/* SPDX-License-Identifier: BSD-2-Clause */
#include "native_usb.h"
#include <errno.h>
#include <string.h>

int
rx_usb_config(struct usbperiph_config *c, uint16_t vid, uint16_t pid)
{
	static const uint8_t device[] = {18,1,0,2,0,0,0,64,0,0,0,0,0,1,1,2,3,1};
	static const uint8_t config[] = {
		9,2,46,0,1,1,0,0x80,250,
		9,4,0,0,4,0xff,0,0,4,
		7,5,0x81,2,0,2,0, 7,5,0x01,2,0,2,0,
		7,5,0x82,2,0,2,0, 7,5,0x02,2,0,2,0
	};
	static const char *const strings[] = {"EmberBSD", "EmberBSD Pluto RX", "RX001", "IIO"};
	size_t offset = 4;
	if (!vid || !pid) return -EINVAL;
	memset(c, 0, sizeof(*c));
	c->version = USBPERIPH_ABI;
	memcpy(c->device, device, sizeof(device));
	c->device[8] = vid & 255; c->device[9] = vid >> 8;
	c->device[10] = pid & 255; c->device[11] = pid >> 8;
	memcpy(c->config, config, sizeof(config));
	c->config_length = sizeof(config);
	c->strings[0] = 4; c->strings[1] = 3; c->strings[2] = 9; c->strings[3] = 4;
	for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
		size_t n = strlen(strings[i]);
		c->strings[offset++] = (uint8_t)(n * 2 + 2);
		c->strings[offset++] = 3;
		for (size_t j = 0; j < n; j++) {
			c->strings[offset++] = (uint8_t)strings[i][j];
			c->strings[offset++] = 0;
		}
	}
	c->strings_length = (uint32_t)offset;
	return 0;
}

int
rx_usb_setup(const uint8_t s[8], unsigned *mask, unsigned *reset)
{
	unsigned pipe = s[2] | (unsigned)s[3] << 8;
	*reset = 0;
	if (s[0] != 0x41 || s[4] || s[5] || s[6] || s[7] || pipe > 1 || s[1] > 2)
		return -EINVAL;
	if (s[1] == 0) {
		if (pipe) return -EINVAL;
		*reset = 3; *mask = 0;
	} else if (s[1] == 1) {
		if (*mask & (1U << pipe)) return -EBUSY;
		*mask |= 1U << pipe;
	} else {
		*reset = 1U << pipe; *mask &= ~(1U << pipe);
	}
	return 0;
}
