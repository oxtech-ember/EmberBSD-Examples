/* SPDX-License-Identifier: BSD-2-Clause */
#include "native_adc.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int calls, fail_status;
static int control(int fd,unsigned long op,void *v)
{
	assert(fd==123);assert(op==ADI_AXI_ADCIOC_STREAM_STATUS);calls++;
	struct adi_axi_adc_stream_status *s=v;
	s->generation=91;s->last_error=EOVERFLOW;s->flags=ADI_AXI_ADC_STREAM_LOSS_UNKNOWN;
	s->block_bytes=262144;s->block_count=4;s->completed_blocks=3;s->completed_bytes=786432;
	s->discontinuities=1;s->dropped_blocks=2;
	if(fail_status){memset(s,0xff,sizeof(*s));errno=EIO;return -1;}return 0;
}
static ssize_t input(int fd,void *p,size_t n,unsigned timeout)
{ (void)p;(void)n;(void)timeout;assert(fd==123);errno=EOVERFLOW;return -1; }
int main(void)
{
	struct rx_adc a;rx_adc_init(&a,123);a.running=1;a.input=input;a.control=control;
	FILE *log=tmpfile();assert(log);int saved=dup(2);assert(saved>=0);assert(dup2(fileno(log),2)>=0);
	char b[4];assert(rx_adc_read(&a,b,sizeof(b))==-EOVERFLOW);
	fail_status=1;assert(rx_adc_read(&a,b,sizeof(b))==-EOVERFLOW);
	for(int i=0;i<20;i++)assert(rx_adc_read(&a,b,sizeof(b))==-EOVERFLOW);
	fflush(stderr);assert(dup2(saved,2)>=0);close(saved);rewind(log);
	char text[8192]={0};assert(fread(text,1,sizeof(text)-1,log)>0);fclose(log);
	assert(strstr(text,"generation=91"));assert(strstr(text,"block_count=4"));
	assert(strstr(text,"completed_bytes=786432"));assert(strstr(text,"dropped_blocks=2"));
	assert(strstr(text,"status_error="));assert(!strstr(text,"18446744073709551615"));
	assert(calls==8);puts("adc error diagnostic PASS: ownfd, bounded, poisoned status discarded");return 0;
}
