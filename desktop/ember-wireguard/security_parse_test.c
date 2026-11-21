/* Parser and owned-child cleanup tests; no privileged or device operations. */
#define seteuid simulated_seteuid
#define main ctl_main
#include "ctl.c"
#undef main
#undef seteuid
#include <assert.h>
int simulated_seteuid(uid_t uid) { (void)uid; return 0; }
static int run_data(const char *text, size_t size) {
 char src[]="/tmp/wg-src-XXXXXX", dst[]="/tmp/wg-dst-XXXXXX";
 char addr[128], prefix[16], mtu[16], allowed[32][128]; size_t n;
 int in=mkstemp(src), out=mkstemp(dst), ret; struct stat st;
 assert(in >= 0 && out >= 0); assert(write(in,text,size)==(ssize_t)size); close(in); close(out);
 ret=prepare_conf(src,dst,addr,sizeof(addr),prefix,sizeof(prefix),mtu,sizeof(mtu),allowed,&n,32);
 if (ret==0) { assert(stat(dst,&st)==0); assert((st.st_mode & 0777)==0600); }
 else assert(access(dst,F_OK)<0);
 unlink(src); unlink(dst); return ret;
}
static int run(const char *text) { return run_data(text, strlen(text)); }
static void test_child_cleanup(void) {
 int ready[2]; char byte; assert(pipe(ready)==0);
 pid_t pid=fork(); assert(pid>=0);
 if (pid==0) {
  close(ready[0]); signal(SIGTERM,SIG_IGN);
  if (write(ready[1],"r",1)!=1) _exit(2);
  close(ready[1]); for (;;) pause();
 }
 close(ready[1]); assert(read(ready[0],&byte,1)==1); close(ready[0]);
 alarm(6); assert(stop_child(pid)==0); alarm(0);
 assert(waitpid(pid,NULL,WNOHANG)==-1 && errno==ECHILD);
}
int main(void) {
 test_child_cleanup();
 assert(run("[Interface]\nAddress = 192.0.2.2/24\nMTU = 1420\n[Peer]\nAllowedIPs = 0.0.0.0/0\n")==0);
 {
  char src[]="/tmp/wg-list-src-XXXXXX", dst[]="/tmp/wg-list-dst-XXXXXX";
  const char *input="[Interface]\nAddress = 192.0.2.2/24\n[Peer]\nAllowedIPs = 198.51.100.0/24, 203.0.113.0/24\n[Peer]\nAllowedIPs = 10.0.0.0/8\n";
  char addr[128], prefix[16], mtu[16], allowed[32][128], output[1024]; size_t n;
  int in=mkstemp(src), out=mkstemp(dst); assert(in>=0 && out>=0);
  assert(write(in,input,strlen(input))==(ssize_t)strlen(input)); close(in); close(out);
  assert(prepare_conf(src,dst,addr,sizeof(addr),prefix,sizeof(prefix),mtu,sizeof(mtu),allowed,&n,32)==0);
  assert(n==3 && strcmp(allowed[0],"198.51.100.0/24")==0 && strcmp(allowed[1],"203.0.113.0/24")==0 && strcmp(allowed[2],"10.0.0.0/8")==0);
  out=open(dst,O_RDONLY); assert(out>=0); ssize_t len=read(out,output,sizeof(output)-1); assert(len>=0); output[len]='\0'; close(out);
  assert(strstr(output,"AllowedIPs = 198.51.100.0/24, 203.0.113.0/24\n")!=NULL);
  assert(strstr(output,"AllowedIPs = 10.0.0.0/8\n")!=NULL);
  unlink(src); unlink(dst);
 }
 assert(run("[Interface]\nAddress = 192.0.2.2/33\n")<0);
 assert(run("[Interface]\nAddress = 2001:db8::1/64\n")<0);
 assert(run("[Interface]\nAddress = 192.0.2.2/24\nMTU = 65536\n")<0);
 assert(run("[Interface]\nAddress = 192.0.2.2/24\n[Peer]\nAllowedIPs = ::/0\n")<0);
 const char nul[]="[Interface]\nAddress = 192.0.2.2/24\n\0[Peer]\nAllowedIPs=0.0.0.0/0\n";
 assert(run_data(nul,sizeof(nul)-1)<0);
 const char *unsupported[]={"Table","PreUp","PostUp","PreDown","PostDown","SaveConfig"};
 for (size_t i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++) {
  char config[256]; snprintf(config,sizeof(config),"[Interface]\nAddress = 192.0.2.2/24\n%s = value\n",unsupported[i]); assert(run(config)<0);
 }
 assert(run("[Interface]\nAddress = 192.0.2.2/24\nDNS = 192.0.2.53\n")==0);
 char many[2000]="[Interface]\nAddress = 192.0.2.2/24\n[Peer]\nAllowedIPs = ";
 for(int i=0;i<33;i++) strcat(many, i ? ",10.0.0.0/8" : "10.0.0.0/8");
 strcat(many,"\n"); assert(run(many)<0);
 return 0;
}
