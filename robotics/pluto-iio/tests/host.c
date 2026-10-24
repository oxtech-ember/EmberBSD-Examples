/* SPDX-License-Identifier: BSD-2-Clause */
/* Synthetic hardware fixture. This executable never accesses a device. */
#include "native_adc.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static uint32_t actual = 2399999;
static int control(int fd, unsigned long op, void *p) {
	(void)fd;
	if (op==ADI_AXI_ADCIOC_CONFIG) { const struct adi_axi_adc_config*c=p; actual=c->sample_rate_hz==3072000?3071999:2399999; return 0; }
	if (op==ADI_AXI_ADCIOC_STREAM_STATUS) { struct adi_axi_adc_stream_status*s=p; s->sample_rate_hz=actual; return 0; }
	if (op==ADI_AXI_ADCIOC_STATUS) { struct adi_axi_adc_status*s=p; s->valid_mask=1;s->gain_mdB[0]=30000;return 0; }
	if (op==ADI_AXI_ADCIOC_STREAM_START||op==ADI_AXI_ADCIOC_STREAM_STOP) return 0;
	errno=EINVAL;return -1;
}
static ssize_t input(int fd, void *v, size_t n, unsigned timeout) {
	unsigned char*b=v;(void)fd;
	(void)timeout;
	/* I = -1 (12 bits), Q = +1. */
	for(size_t i=0;i<n;i+=4){b[i]=255;b[i+1]=15;b[i+2]=1;b[i+3]=0;} return (ssize_t)n;
}
static int output(void*v,unsigned p,const void*b,size_t n) {
	(void)v;(void)p;return fwrite(b,1,n,stdout)==n&&fflush(stdout)==0?0:-EPIPE;
}
int main(void) {
	struct rx_adc a;struct rx_service s;unsigned char b[256];
	struct rx_ops ops={rx_adc_attr,rx_adc_start,rx_adc_read,rx_adc_stop,output,rx_adc_settings,NULL,NULL,NULL,NULL};
	rx_adc_init(&a,-1);a.control=control;a.input=input;rx_init(&s,&ops,&a);
	for(;;){ssize_t n=read(STDIN_FILENO,b,sizeof(b));if(n<=0)break;if(rx_feed(&s,0,b,(size_t)n))break;}
	return rx_disconnect(&s)!=0;
}
