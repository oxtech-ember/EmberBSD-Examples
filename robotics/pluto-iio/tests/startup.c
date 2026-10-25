/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise real daemon exit paths without device access. */
#include <sys/ioctl.h>
#include <stdint.h>
#include <pthread.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
static int scenario, events, lifecycle_gates, connected, replies;
static uint64_t kernel_generation;
static struct server *observed_server;
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
{ observed_server=((struct worker *)v)->server; return scenario==1 ? EAGAIN : pthread_create(t,a,f,v); }
static int fixture_ioctl(int f,unsigned long op,...)
{
	(void)f;
#ifdef USBPERIPH_IOC_SNAPSHOT
	/* A failing diagnostic must not replace the original ESTALE. */
	if(op==USBPERIPH_IOC_SNAPSHOT){errno=EIO;return -1;}
#endif
	if(op==USBPERIPH_IOC_GET_EVENT) {
		va_list ap;va_start(ap,op);struct usbperiph_event *e=va_arg(ap,void *);va_end(ap);
		if((scenario==5 || scenario==6) && events++){assert(lifecycle_gates==4);errno=EIO;return -1;}
		if(scenario==4 && events==35){events++;errno=EIO;return -1;}
		memset(e,0,sizeof(*e));e->type=USBPERIPH_EVENT_RESET;e->generation=42;e->sequence=7;
		if(scenario==2 || scenario==7){e->type=USBPERIPH_EVENT_SETUP;e->setup[0]=0x41;e->setup[1]=scenario==7?1:2;}
		if((scenario==5 || scenario==6)){kernel_generation=scenario==5?43:42;if(scenario==6)e->type=USBPERIPH_EVENT_SUSPEND;}
		if(scenario==4 && ++events<=35){e->type=USBPERIPH_EVENT_CONFIGURE;e->configured=1;}
		return 0;
	}
	if(op==USBPERIPH_IOC_ENDPOINT_GATE) {
		va_list ap;va_start(ap,op);struct usbperiph_endpoint_gate *g=va_arg(ap,void *);va_end(ap);
		if((scenario==5 || scenario==6) && connected){
			if(g->generation && g->generation!=kernel_generation){errno=ESTALE;return -1;}
			assert(g->enable==0);assert(observed_server);
			assert(observed_server->mask==0 && observed_server->service.closing==3);
			assert(observed_server->epoch[0] && observed_server->epoch[1]);
			lifecycle_gates++;kernel_generation++;
		}else if(g->generation){errno=ESTALE;return -1;}
	}
	if(op==USBPERIPH_IOC_REPLY_SETUP)replies++;
	if(op==USBPERIPH_IOC_CONNECT){connected=1;if(scenario==3){errno=EIO;return -1;}}
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
	if(scenario==2 || scenario==7){assert(strstr(text,"stage=handle-setup"));assert(strstr(text,"generation=42"));assert(strstr(text,"sequence=7"));assert(strstr(text,"stage=endpoint-gate"));
		char expected[128];snprintf(expected,sizeof(expected),"stage=handle-setup error=%d",ESTALE);
		assert(strstr(text,expected));assert(replies==0);}
	if(scenario==3)assert(strstr(text,"stage=usb-connect"));
	if(scenario==4){unsigned n=0;const char *p=text;while((p=strstr(p,"stage=event error=0"))){n++;p++;}assert(n==32);assert(events==36);}
	if((scenario==5 || scenario==6)){assert(events==2);assert(!strstr(text,"stage=event-close"));assert(strstr(text,"stage=get-event"));}
	assert(strstr(text,"exit status=1"));puts("startup diagnostic PASS");return 0;
}
