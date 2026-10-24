/* SPDX-License-Identifier: BSD-2-Clause */
#include "iio_rx.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct fixture { char out[65536]; size_t n; int starts, stops, fail; unsigned timeout, blocks; };
static int send_bytes(void *v, unsigned p, const void *b, size_t n) {
	struct fixture *f = v; (void)p;
	assert(n <= sizeof(f->out) - f->n); memcpy(f->out + f->n, b, n); f->n += n; return 0;
}
static int attr(void *v, const char *d, const char *c, const char *a, char *b, size_t n, int w) {
	(void)v; (void)d; (void)c;
	if (strcmp(a, "sampling_frequency")) return -ENOENT;
	if (w) return !strcmp(b, "1536000") ? -ERANGE : 0;
	return snprintf(b, n, "2399999");
}
static int start(void *v, unsigned n) { struct fixture *f=v; assert(n > 0); f->starts++; return 0; }
static int stop(void *v) { ((struct fixture *)v)->stops++; return 0; }
/* The callback signature also serves mutable ADC implementations. */
/* cppcheck-suppress constParameterCallback */
static ssize_t data(void *v, void *b, size_t n) {
	const struct fixture *f=v; if(f->fail) return -EOVERFLOW; memset(b, 0x5a, n); return (ssize_t)n;
}
static int settings(void *v, unsigned timeout, unsigned blocks) { struct fixture *f=v;f->timeout=timeout;f->blocks=blocks;return 0; }
static const struct rx_ops ops = {attr,start,data,stop,send_bytes,settings};
static void feed(struct rx_service *s, struct fixture *f, unsigned p, const char *cmd) {
	f->n=0; memset(f->out,0,sizeof(f->out)); assert(rx_feed(s,p,cmd,strlen(cmd)) == 0);
}
int main(void) {
	struct rx_service s; struct fixture f = {0}; char big[RX_COMMAND_MAX+1];
	rx_init(&s,&ops,&f);
	feed(&s,&f,0,"TIMEOUT 1234\n"); assert(f.timeout==1234);
	feed(&s,&f,0,"SET iio:device1 BUFFERS_COUNT 8\n"); assert(f.blocks==8);
	feed(&s,&f,0,"PRI"); assert(f.n==0);
	feed(&s,&f,0,"NT\r\n"); assert(strstr(f.out,"<context") != NULL);
	feed(&s,&f,0,"ZPRINT\r\n"); assert(!strcmp(f.out,"-22\n"));
	feed(&s,&f,0,"OPEN iio:device1 4096 00000001\r\n"); assert(!strcmp(f.out,"-22\n")); assert(f.starts==0);
	feed(&s,&f,1,"OPEN iio:device1 4096 00000003\r\n"); assert(!strcmp(f.out,"0\n")); assert(f.starts==1);
	feed(&s,&f,0,"READBUF iio:device1 16\r\n"); assert(!strcmp(f.out,"-16\n"));
	feed(&s,&f,1,"READBUF iio:device1 16\r\n"); assert(f.n==28); assert(!memcmp(f.out,"16\n00000003\nZZZZ",16));
	f.fail=1; feed(&s,&f,1,"READBUF iio:device1 16\r\n"); assert(!strcmp(f.out,"-75\n")); assert(!s.active);
	feed(&s,&f,0,"READ iio:device0 INPUT voltage0 missing\r\n"); assert(!strcmp(f.out,"-2\n"));
	feed(&s,&f,0,"WRITE iio:device0 INPUT voltage0 sampling_frequency 7\r\n153"); assert(f.n==0);
	feed(&s,&f,0,"6000"); assert(!strcmp(f.out,"-34\n"));
	feed(&s,&f,0,"READ iio:device0 INPUT voltage0 sampling_frequency\r\n"); assert(!strcmp(f.out,"7\n2399999\n"));
	feed(&s,&f,0,"WRITE iio:device0 INPUT voltage0 unknown 2048\r\n"); assert(!strcmp(f.out,"-7\n"));
	assert(rx_feed(&s,0,"PRINT\n",6)==-EPROTO);
	rx_init(&s,&ops,&f); memset(big,'a',sizeof(big)); assert(rx_feed(&s,0,big,sizeof(big))==-EMSGSIZE);
	rx_init(&s,&ops,&f); feed(&s,&f,1,"OPEN iio:device1 4096 00000003\n"); assert(rx_disconnect(&s)==0); assert(!s.active); assert(s.pipe[0].used==0);
	puts("protocol: PASS"); return 0;
}
