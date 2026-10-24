/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef NATIVE_ADC_H
#define NATIVE_ADC_H
#include <stdint.h>
#include <dev/fdt/adi_axi_adc.h>
#include <dev/fdt/adi_axi_adc_stream.h>
#include "iio_rx.h"
struct rx_adc {
	int fd, running, configured;
	unsigned timeout_ms, blocks;
	struct adi_axi_adc_config config;
	struct adi_axi_adc_stream_config stream;
	struct adi_axi_adc_stream_status status;
	int (*control)(int, unsigned long, void *);
	ssize_t (*input)(int, void *, size_t, unsigned);
};
int rx_adc_init(struct rx_adc *, int);
int rx_adc_attr(void *, const char *, const char *, const char *, char *, size_t, int);
int rx_adc_start(void *, unsigned);
ssize_t rx_adc_read(void *, void *, size_t);
int rx_adc_stop(void *);
int rx_adc_settings(void *, unsigned, unsigned);
#endif
