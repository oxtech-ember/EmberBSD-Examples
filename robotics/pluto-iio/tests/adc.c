/* SPDX-License-Identifier: BSD-2-Clause */
#include "native_adc.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int configs, starts, stops;
static uint32_t actual;
static int control(int fd, unsigned long op, void *p) {
	(void)fd;
	if(op == ADI_AXI_ADCIOC_CONFIG) {
		const struct adi_axi_adc_config *c=p;
		configs++; actual=c->sample_rate_hz==2400000?2399999:3071999; return 0;
	}
	if(op == ADI_AXI_ADCIOC_STREAM_STATUS) {
		struct adi_axi_adc_stream_status *s=p; s->sample_rate_hz=actual; return 0;
	}
	if(op == ADI_AXI_ADCIOC_STREAM_START) { starts++; return 0; }
	if(op == ADI_AXI_ADCIOC_STREAM_STOP) { stops++; return 0; }
	errno=EINVAL; return -1;
}
static ssize_t input(int fd, void *b, size_t n, unsigned timeout) { (void)fd;assert(timeout>0);memset(b,0,n);return (ssize_t)n; }
int main(void) {
	struct rx_adc a; char b[64]; assert(rx_adc_init(&a,-1)==0);
	a.control=control; a.input=input;
	strcpy(b,"2400000"); assert(rx_adc_attr(&a,"iio:device0","INPUT:voltage0","sampling_frequency",b,sizeof(b),1)==0);
	assert(configs==1);
	assert(rx_adc_attr(&a,"iio:device0","INPUT:voltage0","sampling_frequency",b,sizeof(b),0)==7);assert(!strcmp(b,"2399999"));
	strcpy(b,"1536000"); assert(rx_adc_attr(&a,"iio:device0","INPUT:voltage0","sampling_frequency",b,sizeof(b),1)==-ERANGE);assert(configs==1);
	strcpy(b,"1"); assert(rx_adc_attr(&a,"iio:device0","","in_out_voltage_filter_fir_en",b,sizeof(b),1)==-EOPNOTSUPP);
	strcpy(b,"100000000"); assert(rx_adc_attr(&a,"iio:device0","OUTPUT:altvoltage1","frequency",b,sizeof(b),1)==-ENOENT);
	strcpy(b,"0"); assert(rx_adc_attr(&a,"iio:device0","OUTPUT:altvoltage1","powerdown",b,sizeof(b),1)==-EACCES);
	strcpy(b,"1"); assert(rx_adc_attr(&a,"iio:device0","OUTPUT:altvoltage1","powerdown",b,sizeof(b),1)==0);
	strcpy(b,"1"); assert(rx_adc_attr(&a,"iio:device0","INPUT:voltage0","filter_fir_en",b,sizeof(b),1)==-EOPNOTSUPP);
	assert(rx_adc_start(&a,4096)==0);assert(starts==1);
	strcpy(b,"3072000"); assert(rx_adc_attr(&a,"iio:device0","INPUT:voltage0","sampling_frequency",b,sizeof(b),1)==0);assert(stops==1&&starts==2);
	assert(rx_adc_read(&a,b,16)==16);assert(rx_adc_stop(&a)==0);assert(stops==2);
	/* An empty real pipe must time out before the blocking read can start. */
	struct rx_adc timed; int pipes[2]; assert(pipe(pipes)==0);
	rx_adc_init(&timed,pipes[0]); timed.control=control; timed.running=1;
	assert(rx_adc_settings(&timed,10,0)==0);
	assert(rx_adc_read(&timed,b,16)==-ETIMEDOUT);
	close(pipes[0]);close(pipes[1]);
	puts("adc: PASS");return 0;
}
