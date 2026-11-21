/* SPDX-License-Identifier: BSD-2-Clause */
/* Pure route table model; no host network operations. */
#include "routes.h"
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct entry { uint32_t net; unsigned bits; char gateway[32]; int direct; };
struct model {
 struct entry routes[100]; size_t n;
 char endpoints[256]; int fail_add, fail_delete, fail_query, add_calls;
 const char *host_reply;
 struct entry added[100]; size_t nadded;
};
static uint32_t ip(const char *s) { struct in_addr a; assert(inet_pton(AF_INET,s,&a)==1); return ntohl(a.s_addr); }
static uint32_t mask(unsigned n) { return n ? UINT32_MAX << (32-n) : 0; }
static const char *str(uint32_t n, char *b) { struct in_addr a={htonl(n)}; assert(inet_ntop(AF_INET,&a,b,16)); return b; }
static void insert(struct model *m,const char *s,unsigned bits,const char *gw,int direct) {
 struct entry *e=&m->routes[m->n++]; assert(m->n<=100); e->net=ip(s)&mask(bits); e->bits=bits; snprintf(e->gateway,sizeof(e->gateway),"%s",gw); e->direct=direct;
}
static int index_of(struct model *m,uint32_t n,unsigned bits) { for(size_t i=0;i<m->n;i++) if(m->routes[i].net==n&&m->routes[i].bits==bits) return (int)i; return -1; }
static void init(struct model *m) { memset(m,0,sizeof(*m)); insert(m,"0.0.0.0",0,"198.51.100.1",0); strcpy(m->endpoints,"pubkey\t203.0.113.10:51820\n"); }
static int run(void *p,const char *const *a,char *out,size_t size) {
 struct model *m=p; char b[16],mb[16];
 if(strcmp(a[0],"wg")==0) { assert(out); snprintf(out,size,"%s",m->endpoints); return 0; }
 assert(strcmp(a[0],"route")==0);
 if(strcmp(a[1],"-n")==0) {
  assert(strcmp(a[2],"get")==0 && strcmp(a[3],"-inet")==0 && out);
  uint32_t dst=ip(a[4]); int best=-1;
  if(m->fail_query) { snprintf(out,size,"route: routing socket error\n"); return -1; }
  if(a[5]) {
   assert(strcmp(a[5],"-prefixlen")==0); unsigned bits=(unsigned)strtoul(a[6],NULL,10); assert(bits<=32);
   if (bits == 32 && m->host_reply != NULL) {
    snprintf(out, size, "%s", m->host_reply);
    return 0;
   }
   best=index_of(m,dst&mask(bits),bits);
   /* NetBSD host queries can return the best covering network. */
   if(best<0 && bits==32)
    for(size_t i=0;i<m->n;i++) if((dst&mask(m->routes[i].bits))==m->routes[i].net && (best<0 || m->routes[i].bits>m->routes[best].bits)) best=(int)i;
  } else {
   for(size_t i=0;i<m->n;i++) if((dst&mask(m->routes[i].bits))==m->routes[i].net && (best<0 || m->routes[i].bits>m->routes[best].bits)) best=(int)i;
  }
  if(best<0) { snprintf(out,size,"route: not in table\n"); return -1; }
  struct entry *e=&m->routes[best];
  snprintf(out,size,"   route to: %s\n destination: %s\n        mask: %s\n     gateway: %s\n   interface: %s\n  local addr: %s\n       flags: <UP,%s%s>\n",a[4],e->net?str(e->net,b):"default",e->bits?str(mask(e->bits),mb):"default",e->gateway,e->direct?"tun0":"wm0",e->direct?e->gateway:"198.51.100.75",e->direct?"":"GATEWAY,",e->bits==32?"HOST":"STATIC");
  return 0;
 }
 assert(strcmp(a[2],"-inet")==0 && strcmp(a[4],"-prefixlen")==0);
 unsigned bits=(unsigned)strtoul(a[5],NULL,10); assert(bits<=32);
 uint32_t net=ip(a[3])&mask(bits); int direct=strcmp(a[6],"-interface")==0; const char *gw=direct?a[7]:a[6]; assert(gw); (void)ip(gw);
 int pos=index_of(m,net,bits);
 if(strcmp(a[1],"add")==0) {
  if(++m->add_calls==m->fail_add) return -1;
  if(pos>=0) return -1;
  struct entry e={net,bits,"",direct}; snprintf(e.gateway,sizeof(e.gateway),"%s",gw); m->routes[m->n++]=e; m->added[m->nadded++]=e; return 0;
 }
 assert(strcmp(a[1],"delete")==0);
 if(m->fail_delete || pos<0) return -1;
 struct entry *e=&m->routes[pos]; if(e->direct!=direct || strcmp(e->gateway,gw)) return -1;
 memmove(e,e+1,(m->n-(size_t)pos-1)*sizeof(*e)); m->n--; return 0;
}
static int up(struct model *m,const char a[][128],size_t n,const char *j) { return wg_routes_up("route","wg","tun0","192.0.2.8",a,n,j,run,m); }
static int down(struct model *m,const char *j) { return wg_routes_down("route",j,run,m); }
static int exists(struct model *m,const char *a,unsigned bits,const char *g,int direct) { int i=index_of(m,ip(a)&mask(bits),bits); return i>=0 && m->routes[i].direct==direct && strcmp(m->routes[i].gateway,g)==0; }

int main(void) {
 struct model m; char dir[]="/tmp/wg-routes-test-XXXXXX",j[256],target[256]; assert(mkdtemp(dir)); snprintf(j,sizeof(j),"%s/journal",dir); snprintf(target,sizeof(target),"%s/target",dir);
 const char net[][128]={"10.1.2.0/24"}, full[][128]={"0.0.0.0/0"}, bad[][128]={"2001:db8::/64"}, malformed[][128]={"10.0.0.0/33"};
 init(&m); assert(up(&m,net,1,j)==0); assert(exists(&m,"10.1.2.0",24,"192.0.2.8",1)); assert(down(&m,j)==0); assert(m.n==1);
 init(&m); assert(up(&m,full,1,j)==0); assert(m.nadded==3); assert(m.added[0].net==ip("203.0.113.10") && m.added[0].bits==32 && !m.added[0].direct && !strcmp(m.added[0].gateway,"198.51.100.1")); assert(m.added[1].bits==1 && m.added[2].bits==1); assert(exists(&m,"0.0.0.0",0,"198.51.100.1",0)); assert(down(&m,j)==0 && m.n==1);
 init(&m); insert(&m,"10.1.2.0",24,"192.0.2.8",1); assert(up(&m,net,1,j)==0); assert(down(&m,j)==0); assert(exists(&m,"10.1.2.0",24,"192.0.2.8",1));
 init(&m); insert(&m,"128.0.0.0",1,"198.51.100.2",0); assert(up(&m,full,1,j)!=0); assert(m.n==2 && !exists(&m,"203.0.113.10",32,"198.51.100.1",0));
 init(&m); m.fail_add=3; assert(up(&m,full,1,j)!=0); assert(m.n==1 && access(j,F_OK)<0);
 init(&m); assert(up(&m,net,1,j)==0); m.fail_delete=1; assert(down(&m,j)!=0 && access(j,F_OK)==0); m.fail_delete=0; assert(down(&m,j)==0 && m.n==1);
 init(&m); assert(up(&m,net,1,j)==0); int i=index_of(&m,ip("10.1.2.0"),24); assert(i>=0); strcpy(m.routes[i].gateway,"192.0.2.9"); assert(down(&m,j)==0); assert(exists(&m,"10.1.2.0",24,"192.0.2.9",1));
 init(&m); assert(up(&m,bad,1,j)!=0 && m.n==1 && access(j,F_OK)<0); assert(up(&m,malformed,1,j)!=0 && m.n==1 && access(j,F_OK)<0); strcpy(m.endpoints,"pubkey\t(none)\n"); assert(up(&m,full,1,j)!=0 && m.n==1 && access(j,F_OK)<0);
 init(&m); insert(&m,"203.0.113.10",32,"198.51.100.1",0); insert(&m,"0.0.0.0",1,"192.0.2.8",1); assert(up(&m,full,1,j)==0); assert(down(&m,j)==0); assert(exists(&m,"0.0.0.0",1,"192.0.2.8",1));
 init(&m); m.fail_query=1; assert(up(&m,net,1,j)!=0 && m.n==1 && access(j,F_OK)<0);
 /* A covering endpoint network is preserved while a host escape is owned. */
 init(&m); insert(&m,"203.0.113.0",24,"198.51.100.2",0);
 assert(up(&m,full,1,j)==0);
 assert(exists(&m,"203.0.113.10",32,"198.51.100.2",0));
 assert(down(&m,j)==0 && m.n==2);
 assert(exists(&m,"203.0.113.0",24,"198.51.100.2",0));
 assert(exists(&m,"0.0.0.0",0,"198.51.100.1",0));
 /* An already removed endpoint must not make cleanup delete the default. */
 init(&m); assert(up(&m,full,1,j)==0);
 { int endpoint=index_of(&m,ip("203.0.113.10"),32); assert(endpoint>=0);
   memmove(&m.routes[endpoint],&m.routes[endpoint+1],(m.n-(size_t)endpoint-1)*sizeof(m.routes[0])); m.n--; }
 assert(down(&m,j)==0 && m.n==1);
 assert(exists(&m,"0.0.0.0",0,"198.51.100.1",0));
 /* An invalid host lookup must retain ownership for a later retry. */
 const char *invalid_host_replies[] = {
  "destination: 198.51.100.0\nmask: 255.255.255.0\n"
  "gateway: 198.51.100.1\ninterface: wm0\nflags: <UP,GATEWAY,STATIC>\n",
  "destination: 203.0.113.0\nmask: 255.255.255.0\n"
  "gateway: invalid\ninterface: wm0\nflags: <UP,GATEWAY,STATIC>\n",
  "destination: 203.0.113.0\nmask: 255.255.0.255\n"
  "gateway: 198.51.100.1\ninterface: wm0\nflags: <UP,GATEWAY,STATIC>\n"
 };
 for (size_t k = 0; k < sizeof(invalid_host_replies) / sizeof(invalid_host_replies[0]); k++) {
  init(&m);
  assert(up(&m, full, 1, j) == 0);
  m.host_reply = invalid_host_replies[k];
  assert(down(&m, j) != 0 && access(j, F_OK) == 0);
  assert(exists(&m, "203.0.113.10", 32, "198.51.100.1", 0));
  m.host_reply = NULL;
  assert(down(&m, j) == 0 && m.n == 1 && access(j, F_OK) < 0);
  assert(exists(&m, "0.0.0.0", 0, "198.51.100.1", 0));
 }
 /* Exact lookups distinguish an owned /24 from an overlapping /32. */
 init(&m); assert(up(&m,net,1,j)==0); insert(&m,"10.1.2.0",32,"198.51.100.2",0); assert(down(&m,j)==0); assert(exists(&m,"10.1.2.0",32,"198.51.100.2",0)); assert(!exists(&m,"10.1.2.0",24,"192.0.2.8",1));
 /* Transient lookup errors are not absence, and retain the journal. */
 init(&m); assert(up(&m,net,1,j)==0); m.fail_query=1; assert(down(&m,j)!=0 && access(j,F_OK)==0); assert(exists(&m,"10.1.2.0",24,"192.0.2.8",1)); m.fail_query=0; assert(down(&m,j)==0);
 /* An interrupted add cannot establish ownership of an existing route. */
 init(&m); insert(&m,"10.1.2.0",24,"192.0.2.8",1); int pending=open(j,O_WRONLY|O_CREAT|O_EXCL,0600); assert(pending>=0); const char *intent="10.1.2.0/24 192.0.2.8 1 0\n"; assert(write(pending,intent,strlen(intent))==(ssize_t)strlen(intent)); close(pending); assert(down(&m,j)!=0 && access(j,F_OK)==0); assert(exists(&m,"10.1.2.0",24,"192.0.2.8",1)); assert(unlink(j)==0);
 /* A second managed profile cannot borrow the first profile's endpoint. */
 { char first[256], second[256];
   snprintf(first,sizeof(first),"%s/first.routes",dir);
   snprintf(second,sizeof(second),"%s/second.routes",dir);
   init(&m); assert(up(&m,full,1,first)==0);
   assert(exists(&m,"203.0.113.10",32,"198.51.100.1",0));
   assert(up(&m,full,1,second)!=0);
   assert(access(second,F_OK)<0 && access(first,F_OK)==0);
   assert(exists(&m,"203.0.113.10",32,"198.51.100.1",0));
   assert(exists(&m,"0.0.0.0",1,"192.0.2.8",1));
   assert(exists(&m,"128.0.0.0",1,"192.0.2.8",1));
   assert(down(&m,first)==0 && m.n==1);
 }
 /* Failed rollback retains evidence for a later cleanup. */
 init(&m); m.fail_add=3; m.fail_delete=1; assert(up(&m,full,1,j)!=0 && access(j,F_OK)==0); assert(m.n==3); m.fail_delete=0; assert(down(&m,j)==0 && m.n==1);
 int fd=open(j,O_WRONLY|O_CREAT|O_EXCL,0644); assert(fd>=0); assert(fchmod(fd,0644)==0); close(fd); assert(down(&m,j)!=0); assert(unlink(j)==0);
 fd=open(target,O_WRONLY|O_CREAT|O_EXCL,0600); assert(fd>=0); close(fd); assert(symlink(target,j)==0); assert(down(&m,j)!=0 && access(target,F_OK)==0); assert(up(&m,net,1,j)!=0 && m.n==1 && access(target,F_OK)==0); unlink(j); unlink(target); assert(rmdir(dir)==0);
 puts("routes tests passed"); return 0;
}
