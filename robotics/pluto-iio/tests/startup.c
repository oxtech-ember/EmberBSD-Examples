/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise real daemon exit paths without device access. */
#include <sys/ioctl.h>
#include <pthread.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
static int scenario, events;
static int fixture_open(const char *, int, ...);
static int fixture_close(int);
static int fixture_ioctl(int, unsigned long, ...);
static int fixture_thread(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
#define open fixture_open
#define close fixture_close
#define ioctl fixture_ioctl
#define pthread_create fixture_thread
#define rx_adc_attr fixture_attr
#define main daemon_main
#include "../src/pluto_iiod.c"
#undef main
#undef rx_adc_attr
#undef pthread_create
#undef ioctl
#undef close
#undef open
static int fixture_open(const char *p, int f, ...) { (void)p;(void)f;return 100; }
static int fixture_close(int f) { (void)f;return 0; }
int fixture_attr(void *a,const char *d,const char *c,const char *n,char *b,size_t z,int w)
{ (void)a;(void)d;(void)c;(void)n;(void)b;(void)z;(void)w;return 0; }
static int fixture_thread(pthread_t *t,const pthread_attr_t *a,void *(*f)(void *),void *v)
{ return scenario==1 ? EAGAIN : pthread_create(t,a,f,v); }
static int fixture_ioctl(int f,unsigned long op,...)
{
	(void)f;
#ifdef USBPERIPH_IOC_SNAPSHOT
	/* A failing diagnostic must not replace the original ESTALE. */
	if(op==USBPERIPH_IOC_SNAPSHOT){errno=EIO;return -1;}
#endif
	if(op==USBPERIPH_IOC_GET_EVENT) {
		va_list ap;va_start(ap,op);struct usbperiph_event *e=va_arg(ap,void *);va_end(ap);
		memset(e,0,sizeof(*e));e->type=USBPERIPH_EVENT_RESET;e->generation=42;e->sequence=7;
		if(scenario==4 && ++events<=35){e->type=USBPERIPH_EVENT_CONFIGURE;e->configured=1;}
		return 0;
	}
	if(op==USBPERIPH_IOC_ENDPOINT_GATE) {
		va_list ap;va_start(ap,op);struct usbperiph_endpoint_gate *g=va_arg(ap,void *);va_end(ap);
		if(g->generation){errno=ESTALE;return -1;}
	}
	if(op==USBPERIPH_IOC_CONNECT && scenario==3){errno=EIO;return -1;}
	return 0;
}
int main(int argc,char **argv)
{
	assert(argc==2);scenario=atoi(argv[1]);
	FILE *log=tmpfile();assert(log);int saved=dup(STDERR_FILENO);assert(saved>=0);
	assert(dup2(fileno(log),STDERR_FILENO)>=0);
	char *args[]={"pluto-iiod","fixture-usb","fixture-adc","456","b673",NULL};
	assert(daemon_main(5,args)==1);fflush(stderr);
	assert(dup2(saved,STDERR_FILENO)>=0);close(saved);rewind(log);
	char text[32768]={0};assert(fread(text,1,sizeof(text)-1,log)>0);fclose(log);
	if(scenario==1){assert(strstr(text,"stage=worker-create"));assert(strstr(text,"error=35") || strstr(text,"error=11"));}
	if(scenario==2){assert(strstr(text,"stage=event-close"));assert(strstr(text,"generation=42"));assert(strstr(text,"sequence=7"));assert(strstr(text,"stage=endpoint-gate"));
		char expected[128];snprintf(expected,sizeof(expected),"stage=event-close error=%d",ESTALE);
		assert(strstr(text,expected));}
	if(scenario==3)assert(strstr(text,"stage=usb-connect"));
	if(scenario==4){unsigned n=0;const char *p=text;while((p=strstr(p,"stage=event error=0"))){n++;p++;}assert(n==32);assert(events==36);}
	assert(strstr(text,"exit status=1"));puts("startup diagnostic PASS");return 0;
}
