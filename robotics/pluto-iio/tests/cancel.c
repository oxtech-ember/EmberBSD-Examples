/* SPDX-License-Identifier: BSD-2-Clause */
/* Real worker and EP0 handler; only device system calls are fixtures. */
#include <sys/ioctl.h>
#include <stdarg.h>
#include <assert.h>
static int fixture_ioctl(int, unsigned long, ...);
#define ioctl fixture_ioctl
#define main daemon_main
#include "../src/pluto_iiod.c"
#undef main
#undef ioctl
#include <time.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static int entered_read, cancelled, delivered, after_cancel_in, replies, adc_stops, mode;
static int adc_released, completed_read;
static int endpoint_closed[256];

static int
fixture_ioctl(int fd, unsigned long op, ...)
{
	va_list args; void *arg;
	(void)fd;
	if(op!=USBPERIPH_IOC_XFER&&op!=USBPERIPH_IOC_CANCEL&&
	    op!=ADI_AXI_ADCIOC_STREAM_STOP&&op!=USBPERIPH_IOC_REPLY_SETUP&&
	    op!=USBPERIPH_IOC_ENDPOINT_GATE){errno=EOPNOTSUPP;return -1;}
	va_start(args, op); arg = va_arg(args, void *); va_end(args);
	pthread_mutex_lock(&gate);
	if (op == USBPERIPH_IOC_XFER) {
		struct usbperiph_xfer *x = arg;
		if(mode==5) {
			assert(x->actual==0);x->actual=UINT32_MAX;x->generation=UINT64_MAX;
			pthread_mutex_unlock(&gate);errno=EIO;return -1;
		}
		if (x->endpoint & 0x80) {
			if (cancelled) after_cancel_in++;
			if(mode==4) {
				entered_read=1;pthread_cond_broadcast(&ready);
				while(!endpoint_closed[x->endpoint])pthread_cond_wait(&ready,&gate);
			}
			if (endpoint_closed[x->endpoint]) { pthread_mutex_unlock(&gate); errno=ECANCELED; return -1; }
			x->actual=x->length; pthread_mutex_unlock(&gate); return 0;
		}
		if (!delivered) {
			const char command[]="READBUF iio:device1 16\n";
			memcpy((void *)(uintptr_t)x->buffer,command,sizeof(command)-1);
			x->actual=sizeof(command)-1;delivered=1;
			pthread_mutex_unlock(&gate);return 0;
		}
		pthread_mutex_unlock(&gate);errno=ETIMEDOUT;return -1;
	}
	if (op == USBPERIPH_IOC_CANCEL) { cancelled=1; pthread_mutex_unlock(&gate);return 0; }
	if (op == ADI_AXI_ADCIOC_STREAM_STOP) {
		adc_stops++;cancelled=1;adc_released=1;pthread_cond_broadcast(&ready);
		pthread_mutex_unlock(&gate);return 0;
	}
	if (op == USBPERIPH_IOC_REPLY_SETUP) {
		assert(!((struct usbperiph_reply *)arg)->stall);replies++;
		pthread_mutex_unlock(&gate);return 0;
	}
	if (op == USBPERIPH_IOC_ENDPOINT_GATE) {
		const struct usbperiph_endpoint_gate *g=arg;
		if(mode==6){pthread_mutex_unlock(&gate);errno=EIO;return -1;}
		endpoint_closed[g->endpoint]=!g->enable;
		if (!g->enable) {cancelled=1;pthread_cond_broadcast(&ready);}
		pthread_mutex_unlock(&gate);return 0;
	}
	pthread_mutex_unlock(&gate);errno=EOPNOTSUPP;return -1;
}

static int
control(int fd, unsigned long op, void *p)
{
	(void)fd;(void)p;
	if(op==ADI_AXI_ADCIOC_STREAM_STOP)return 0;
	errno=EINVAL;return -1;
}

static ssize_t
blocked_input(void *v, void *b, size_t n)
{
	if(mode==4){memset(b,0,n);return (ssize_t)n;}
	(void)v;pthread_mutex_lock(&gate);entered_read=1;pthread_cond_broadcast(&ready);
	while(!adc_released)pthread_cond_wait(&ready,&gate);
	completed_read=1;
	pthread_mutex_unlock(&gate);
	if(mode==1)return -ECANCELED;
	memset(b,0,n);return (ssize_t)n;
}

int
main(int argc, char **argv)
{
	assert(argc==2);mode=atoi(argv[1]);
	struct server s=SERVER_INITIALIZER; s.mask=3;s.generation=7;
	if(mode==5){const char bytes[4]={0};int e=transfer(&s,0,0x81,bytes,4,7,0);assert(e==-EIO);puts("cancel: ioctl error output fields ignored");return 0;}
	if(mode==6){struct usbperiph_event ev={.generation=7,.sequence=9,.setup={0x41,2,1,0,0,0,0,0}};
		int e=handle_setup(&s,&ev);assert(e==-EIO);assert(replies==0);puts("cancel: failed gate drain gets no close ACK");return 0;}
	struct rx_ops ops={attribute,start,blocked_input,stop,send_bytes,settings,state_lock,state_unlock,begin_read,end_read};
	struct worker w={&s,1};pthread_t thread;
	rx_adc_init(&s.adc,-1);s.adc.control=control;rx_init(&s.service,&ops,&s);
	s.service.active=1;s.service.owner=1;s.adc.running=1;
	assert(pthread_create(&thread,NULL,work,&w)==0);
	pthread_mutex_lock(&gate);while(!entered_read)pthread_cond_wait(&ready,&gate);pthread_mutex_unlock(&gate);
	struct usbperiph_event event={.generation=7,.sequence=9,.type=USBPERIPH_EVENT_SETUP,
	    .setup={0x41,2,1,0,0,0,0,0}};
	if(mode==3)event.setup[2]=0;
	struct timespec before,after;clock_gettime(CLOCK_MONOTONIC,&before);
	int setup_error=handle_setup(&s,&event);assert(setup_error==0);clock_gettime(CLOCK_MONOTONIC,&after);
	assert(after_cancel_in==0);assert(replies==1);
	if(mode==3){pthread_mutex_lock(&gate);assert(adc_stops==0&&completed_read==0);pthread_mutex_unlock(&gate);}
	assert((after.tv_sec-before.tv_sec)*1000000000LL+after.tv_nsec-before.tv_nsec<500000000LL);
	pthread_mutex_lock(&s.state_lock);s.quitting=1;pthread_cond_broadcast(&s.changed);pthread_mutex_unlock(&s.state_lock);
	pthread_mutex_lock(&gate);adc_released=1;pthread_cond_broadcast(&ready);pthread_mutex_unlock(&gate);
	assert(pthread_join(thread,NULL)==0);
	puts("cancel: no post-close IN; EP0 reply below 500ms");return 0;
}
