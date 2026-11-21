/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * ember-wireguard-ctl: privileged up/down helper for the tray.
 * Installed setuid-root. Accepts only safe tunnel basenames and fixed paths.
 */
#include "routes.h"
#include <ctype.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef __NetBSD__
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <net/if.h>
#endif
#include <unistd.h>

#define RUNDIR "/var/run/ember-wireguard"
#define SYSCONF "/usr/pkg/etc/wireguard"
#define WG "/usr/pkg/bin/wg"
#define WGO "/usr/pkg/bin/wireguard-go"
#define IFCONFIG "/sbin/ifconfig"
#define ROUTE "/sbin/route"
#define OPERATOR_GROUP "wireguard"

/* User config dirs under $HOME/.config — prefer ember-wireguard. */
static const char *user_conf_dirs[] = {
	".config/ember-wireguard",
	".config/wireguard",
	NULL
};

static int
valid_name(const char *name)
{
	size_t n;

	if (name == NULL || *name == '\0')
		return 0;
	n = strlen(name);
	if (n > 64)
		return 0;
	for (; *name; name++) {
		if (!(isalnum((unsigned char)*name) || *name == '-' || *name == '_'))
			return 0;
	}
	return 1;
}

static void
drop_env(void)
{
	static char *safe_env[] = { "PATH=/usr/pkg/bin:/sbin:/usr/sbin:/bin:/usr/bin", "LC_ALL=C", NULL };
	extern char **environ;
	environ = safe_env;
}

static int
may_change_tunnel(uid_t user)
{
	const struct group *group;
	gid_t groups[NGROUPS_MAX];
	int count, i;

	if (user == 0)
		return 1;

	group = getgrnam(OPERATOR_GROUP);
	if (group == NULL)
		return 0;
	count = getgroups(NGROUPS_MAX, groups);
	if (count < 0)
		return 0;
	if (getgid() == group->gr_gid)
		return 1;
	for (i = 0; i < count; i++) {
		if (groups[i] == group->gr_gid)
			return 1;
	}
	return 0;
}

/* Drain the pipe even after the bounded capture fills; never log its contents. */
static int
route_runner(void *ctx, const char *const *argv, char *out, size_t cap)
{
	int fds[2], status, overflow = 0;
	pid_t pid, waited;
	size_t used = 0;
	char buf[4096];
	ssize_t n;
	(void)ctx;
	if (out != NULL && cap > 0) out[0] = '\0';
	if (pipe(fds) < 0) return -1;
	pid = fork();
	if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
	if (pid == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fds[1], STDERR_FILENO) < 0) _exit(127);
		close(fds[1]);
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	close(fds[1]);
	for (;;) {
		n = read(fds[0], buf, sizeof(buf));
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) { if (n < 0) overflow = 1; break; }
		if (out != NULL && cap > 0) {
			size_t avail = cap - 1 - used, take = (size_t)n < avail ? (size_t)n : avail;
			memcpy(out + used, buf, take); used += take; out[used] = '\0';
			if (take < (size_t)n) overflow = 1;
		}
	}
	close(fds[0]);
	do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
	return !overflow && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int
run_cmd(char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	return -1;
}

static int
runtime_path(char *out, size_t outsz, const char *name, const char *suffix)
{
	int n = snprintf(out, outsz, "%s/%lu-%s%s", RUNDIR,
	    (unsigned long)getuid(), name, suffix);
	return n >= 0 && (size_t)n < outsz ? 0 : -1;
}

static int
path_exists(const char *path)
{
	struct stat st;
	if (lstat(path, &st) == 0) return 1;
	return errno == ENOENT ? 0 : -1;
}

static int
ensure_rundir(void)
{
	struct stat st;

	if (mkdir(RUNDIR, 0700) < 0 && errno != EEXIST)
		return -1;
	if (lstat(RUNDIR, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != 0)
		return -1;
	if (chmod(RUNDIR, 0700) < 0)
		return -1;
	return 0;
}

/* Shared serialization across users and tunnel names. */
static int
runtime_lock(void)
{
	struct stat st;
	int fd;

	if (ensure_rundir() < 0)
		return -1;
	fd = open(RUNDIR "/.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    fchmod(fd, 0600) < 0 || flock(fd, LOCK_EX) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int
find_conf(const char *name, uid_t user, char *out, size_t outsz)
{
	struct passwd *pw;
	char path[PATH_MAX];
	int i;

	pw = getpwuid(user);
	for (i = 0; pw != NULL && pw->pw_dir != NULL && user_conf_dirs[i] != NULL; i++) {
		if (snprintf(path, sizeof(path), "%s/%s/%s.conf", pw->pw_dir,
		    user_conf_dirs[i], name) < (int)sizeof(path) && access(path, R_OK) == 0 &&
		    snprintf(out, outsz, "%s", path) < (int)outsz)
			return 0;
	}
	if (snprintf(path, sizeof(path), "%s/%s.conf", SYSCONF, name) < (int)sizeof(path) &&
	    access(path, R_OK) == 0 && snprintf(out, outsz, "%s", path) < (int)outsz)
		return 0;
	return -1;
}

static int
read_file(const char *path, char **buf, size_t *len)
{
	struct stat st;
	char *p = NULL, tmp[4096];
	size_t n = 0, cap = 0;
	ssize_t got;
	int fd, saved_errno;

	if (seteuid(getuid()) < 0)
		return -1;
	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	saved_errno = errno;
	if (seteuid(0) < 0) {
		if (fd >= 0) close(fd);
		return -1;
	}
	errno = saved_errno;
	if (fd < 0) return -1;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
	    st.st_size < 0 || st.st_size > 1024 * 1024) goto fail;
	for (;;) {
		got = read(fd, tmp, sizeof(tmp));
		if (got < 0 && errno == EINTR) continue;
		if (got < 0) goto fail;
		if (got == 0) break;
		if (memchr(tmp, '\0', (size_t)got) != NULL ||
		    (size_t)got > 1024 * 1024 - n) goto fail;
		if (n + (size_t)got + 1 > cap) {
			size_t next = cap ? cap * 2 : 8192;
			char *q;
			while (next < n + (size_t)got + 1) next *= 2;
			q = realloc(p, next);
			if (q == NULL) goto fail;
			p = q; cap = next;
		}
		memcpy(p + n, tmp, (size_t)got);
		n += (size_t)got;
	}
	if (close(fd) < 0) { free(p); return -1; }
	if (p == NULL && (p = malloc(1)) == NULL) return -1;
	p[n] = '\0'; *buf = p; *len = n;
	return 0;
fail:
	close(fd); free(p); return -1;
}

static void
trim_inplace(char *s)
{
	char *e;

	while (*s && isspace((unsigned char)*s))
		memmove(s, s + 1, strlen(s));
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		*--e = '\0';
}

static int
parse_uint(const char *s, unsigned long max)
{
	char *end;
	unsigned long v;

	if (*s == '\0' || !isdigit((unsigned char)*s))
		return 0;
	errno = 0;
	v = strtoul(s, &end, 10);
	return errno == 0 && *end == '\0' && v <= max;
}

static int
valid_ipv4_cidr(const char *s)
{
	char copy[128], *slash;
	struct in_addr a;

	if (snprintf(copy, sizeof(copy), "%s", s) >= (int)sizeof(copy))
		return 0;
	slash = strchr(copy, '/');
	if (slash != NULL) {
		*slash++ = '\0';
		if (!parse_uint(slash, 32))
			return 0;
	}
	return inet_pton(AF_INET, copy, &a) == 1;
}

/* Strip wg-quick keys; capture Address, MTU, AllowedIPs. */
static int
prepare_conf(const char *src, const char *dst_wg, char *addr, size_t addrsz,
    char *prefix, size_t prefixsz, char *mtu, size_t mtusz,
    char allowed[][128], size_t *nallowed, size_t maxallowed)
{
	char *text = NULL;
	size_t len = 0;
	FILE *out;
	char *line, *save = NULL;
	char section[64] = "";
	int have_addr = 0;

	snprintf(addr, addrsz, "%s", "");
	snprintf(prefix, prefixsz, "%s", "32");
	snprintf(mtu, mtusz, "%s", "1420");
	*nallowed = 0;

	if (read_file(src, &text, &len) < 0) {
		unlink(dst_wg);
		return -1;
	}
	{
		int fd = open(dst_wg, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW |
		    O_CLOEXEC, 0600);
		if (fd < 0) {
			free(text);
			return -1;
		}
		if (fchmod(fd, 0600) < 0 || (out = fdopen(fd, "w")) == NULL) {
			close(fd);
			unlink(dst_wg);
			free(text);
			return -1;
		}
	}
	for (line = strtok_r(text, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *t = line;
		char key[64], val[512];
		char *eq;
		int skip = 0;

		while (*t == '\r')
			t++;
		trim_inplace(t);
		if (*t == '\0' || *t == '#' || *t == ';') {
			fprintf(out, "%s\n", line);
			continue;
		}
		if (*t == '[' && t[strlen(t) - 1] == ']') {
			snprintf(section, sizeof(section), "%.*s",
			    (int)strlen(t) - 2, t + 1);
			fprintf(out, "%s\n", line);
			continue;
		}
		eq = strchr(t, '=');
		if (eq == NULL) {
			fprintf(out, "%s\n", line);
			continue;
		}
		*eq = '\0';
		if (snprintf(key, sizeof(key), "%s", t) >= (int)sizeof(key) ||
		    snprintf(val, sizeof(val), "%s", eq + 1) >= (int)sizeof(val))
			goto invalid;
		trim_inplace(key);
		trim_inplace(val);
		if (strcmp(section, "Interface") == 0 && strcmp(key, "Address") == 0) {
			char *slash;
			char first[256];
			char *comma;

			if (snprintf(first, sizeof(first), "%s", val) >= (int)sizeof(first))
				goto invalid;
			comma = strchr(first, ',');
			if (comma != NULL || !valid_ipv4_cidr(first))
				goto invalid;
			trim_inplace(first);
			slash = strchr(first, '/');
			if (slash) {
				*slash = '\0';
				snprintf(addr, addrsz, "%s", first);
				snprintf(prefix, prefixsz, "%s", slash + 1);
			} else {
				snprintf(addr, addrsz, "%s", first);
				snprintf(prefix, prefixsz, "%s", "32");
			}
			have_addr = addr[0] != '\0';
			skip = 1;
		} else if (strcmp(section, "Interface") == 0 && strcmp(key, "MTU") == 0) {
			if (!parse_uint(val, 65535) || strtoul(val, NULL, 10) == 0 ||
			    snprintf(mtu, mtusz, "%s", val) >= (int)mtusz)
				goto invalid;
			skip = 1;
		} else if (strcmp(section, "Interface") == 0 && strcmp(key, "DNS") == 0) {
			skip = 1;
		} else if (strcmp(section, "Interface") == 0 &&
		    (strcmp(key, "Table") == 0 || strcmp(key, "PreUp") == 0 ||
		    strcmp(key, "PostUp") == 0 || strcmp(key, "PreDown") == 0 ||
		    strcmp(key, "PostDown") == 0 || strcmp(key, "SaveConfig") == 0)) {
			goto invalid;
		} else if (strcmp(section, "Peer") == 0 &&
		    strcmp(key, "AllowedIPs") == 0) {
			char copy[sizeof(val)];
			char *p = copy;
			char *tok;

			memcpy(copy, val, strlen(val) + 1);
			while ((tok = strsep(&p, ",")) != NULL) {
				trim_inplace(tok);
				if (*tok == '\0')
					continue;
				if (*nallowed >= maxallowed || !valid_ipv4_cidr(tok) ||
				    snprintf(allowed[*nallowed], 128, "%s", tok) >= 128)
					goto invalid;
				(*nallowed)++;
			}
		}
		if (!skip)
			fprintf(out, "%s = %s\n", key, val);
	}
	if (fclose(out) != 0) {
		free(text);
		unlink(dst_wg);
		return -1;
	}
	free(text);
	if (!have_addr)
		unlink(dst_wg);
	return have_addr ? 0 : -1;
invalid:
	fclose(out);
	free(text);
	unlink(dst_wg);
	return -1;
}

static int
iface_busy(const char *iface)
{
	char path[128];
	char *argv[] = { IFCONFIG, (char *)iface, NULL };

	snprintf(path, sizeof(path), "/dev/%s", iface);
	(void)path;
	return run_cmd(argv) == 0;
}

static int
pick_tun(char *iface, size_t ifsz)
{
	int i;

	for (i = 0; i < 64; i++) {
		snprintf(iface, ifsz, "tun%d", i);
		if (!iface_busy(iface))
			return 0;
	}
	return -1;
}

#ifdef __NetBSD__
static int
process_identity(pid_t pid, unsigned *sec, unsigned *usec)
{
	int mib[] = { CTL_KERN, KERN_PROC2, KERN_PROC_PID, pid,
	    sizeof(struct kinfo_proc2), 1 };
	struct kinfo_proc2 proc;
	size_t len = sizeof(proc);

	if (sysctl(mib, 6, &proc, &len, NULL, 0) < 0)
		return -1;
	if (len == 0)
		return 1;
	if (len != sizeof(proc))
		return -1;
	if (proc.p_pid != pid || proc.p_uid != 0 ||
	    strcmp(proc.p_comm, "wireguard-go") != 0)
		return 1;
	if (!proc.p_uvalid)
		return -1;
	*sec = proc.p_ustart_sec;
	*usec = proc.p_ustart_usec;
	return 0;
}
#else
static int
process_identity(pid_t pid, unsigned *sec, unsigned *usec)
{
	(void)pid; (void)sec; (void)usec;
	return -1;
}
#endif

struct state_meta {
	char iface[32], addr[128], marker[64];
	pid_t pid;
	unsigned sec, usec;
};

static int
write_state(const char *name, const char *iface, const char *addr,
    const char *marker, pid_t pid)
{
	char path[PATH_MAX], temp[PATH_MAX];
	unsigned sec, usec;
	int fd, dirfd, rc = -1;
	FILE *f;
	if (process_identity(pid, &sec, &usec) != 0 ||
	    runtime_path(path, sizeof(path), name, ".state") < 0 ||
	    snprintf(temp, sizeof(temp), "%s.new.XXXXXX", path) >= (int)sizeof(temp))
		return -1;
	dirfd = open(RUNDIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0) return -1;
	fd = mkstemp(temp);
	if (fd < 0) { close(dirfd); return -1; }
	f = fdopen(fd, "w");
	if (f == NULL) { close(fd); unlink(temp); close(dirfd); return -1; }
	if (fprintf(f, "iface=%s\naddr=%s\nmarker=%s\npid=%d\nstart=%u %u\n",
	    iface, addr, marker, (int)pid, sec, usec) >= 0 &&
	    fflush(f) == 0 && fsync(fd) == 0) rc = 0;
	if (fclose(f) != 0) rc = -1;
	if (rc == 0 && (rename(temp, path) != 0 || fsync(dirfd) != 0)) rc = -1;
	if (rc != 0) unlink(temp);
	close(dirfd);
	return rc;
}

static int
read_meta(const char *name, struct state_meta *m)
{
	char path[PATH_MAX], line[256], extra;
	FILE *f;
	int got = 0, pid;
	memset(m, 0, sizeof(*m));
	if (runtime_path(path, sizeof(path), name, ".state") < 0 || (f = fopen(path, "r")) == NULL)
		return -1;
	while (fgets(line, sizeof(line), f) != NULL) {
		if (sscanf(line, "iface=%31s %c", m->iface, &extra) == 1) got |= 1;
		else if (sscanf(line, "addr=%127s %c", m->addr, &extra) == 1) got |= 2;
		else if (sscanf(line, "marker=%63s %c", m->marker, &extra) == 1) got |= 4;
		else if (sscanf(line, "pid=%d %c", &pid, &extra) == 1) { m->pid = pid; got |= 8; }
		else if (sscanf(line, "start=%u %u %c", &m->sec, &m->usec, &extra) == 2) got |= 16;
	}
	if (ferror(f)) got = 0;
	fclose(f);
	return got == 31 && m->pid > 1 && strncmp(m->iface, "tun", 3) == 0 &&
	    strspn(m->iface + 3, "0123456789") == strlen(m->iface + 3) &&
	    m->iface[3] != '\0' && valid_ipv4_cidr(m->addr) ? 0 : -1;
}

/* 1 matches; 0 interface no longer exists; -1 mismatch or error. */
static int
interface_marker(const char *iface, char *marker, int set)
{
#ifdef __NetBSD__
	struct ifreq ifr;
	char descr[IFDESCRSIZE] = "";
	int fd, result, saved;
	memset(&ifr, 0, sizeof(ifr));
	if (snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface) >= (int)sizeof(ifr.ifr_name)) return -1;
	if (set) snprintf(descr, sizeof(descr), "%s", marker);
	ifr.ifr_buf = descr; ifr.ifr_buflen = sizeof(descr);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	result = ioctl(fd, set ? SIOCSIFDESCR : SIOCGIFDESCR, &ifr); saved = errno;
	close(fd);
	if (result < 0) return saved == ENXIO || saved == ENODEV ? 0 : -1;
	return set || strcmp(descr, marker) == 0 ? 1 : -1;
#else
	(void)iface; (void)marker; (void)set; return -1;
#endif
}

static int
destroy_marked(const char *iface, char *marker)
{
	int matched = interface_marker(iface, marker, 0);
	if (matched == 0) return 0;
	if (matched != 1) return -1;
	return run_cmd((char *[]){ IFCONFIG, (char *)iface, "destroy", NULL });
}

static int
state_check(const struct state_meta *m)
{
	unsigned sec, usec;
	int result = process_identity(m->pid, &sec, &usec);
	if (result != 0)
		return result;
	return sec == m->sec && usec == m->usec ? 0 : 1;
}

static int
state_healthy(const char *name, struct state_meta *m)
{
	char journal[PATH_MAX];
	return state_check(m) == 0 && interface_marker(m->iface, m->marker, 0) == 1 &&
	    runtime_path(journal, sizeof(journal), name, ".routes") == 0 &&
	    path_exists(journal) == 1;
}

static int
read_state(const char *name, char *iface, size_t ifsz, pid_t *pid)
{
	char path[PATH_MAX];
	FILE *f;
	char line[256];

	snprintf(iface, ifsz, "%s", "");
	*pid = -1;
	if (runtime_path(path, sizeof(path), name, ".state") < 0)
		return -1;
	f = fopen(path, "r");
	if (f == NULL)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "iface=", 6) == 0) {
			char *v = line + 6;
			trim_inplace(v);
			snprintf(iface, ifsz, "%s", v);
		} else if (strncmp(line, "pid=", 4) == 0) {
			*pid = (pid_t)atoi(line + 4);
		}
	}
	fclose(f);
	return iface[0] ? 0 : -1;
}

static void
clear_state(const char *name)
{
	char path[PATH_MAX];

	if (runtime_path(path, sizeof(path), name, ".state") < 0)
		return;
	unlink(path);
	if (runtime_path(path, sizeof(path), name, ".wg.conf") < 0)
		return;
	unlink(path);
	if (runtime_path(path, sizeof(path), name, ".log") < 0)
		return;
	unlink(path);
}

/* This PID is an unreaped direct child, so it cannot be reused. */
static int
stop_child(pid_t pid)
{
	const int signals[] = { SIGTERM, SIGKILL };
	pid_t result;
	for (size_t phase = 0; phase < sizeof(signals) / sizeof(signals[0]); phase++) {
		do { result = waitpid(pid, NULL, WNOHANG); } while (result < 0 && errno == EINTR);
		if (result == pid)
			return 0;
		if (result < 0 || (kill(pid, signals[phase]) < 0 && errno != ESRCH))
			return -1;
		for (unsigned attempt = 0; attempt < 20; attempt++) {
			usleep(100000);
			do { result = waitpid(pid, NULL, WNOHANG); } while (result < 0 && errno == EINTR);
			if (result == pid)
				return 0;
			if (result < 0)
				return -1;
		}
	}
	fprintf(stderr, "tunnel child did not stop; cleanup required\n");
	return -1;
}

static int cmd_down(const char *);

static int
cmd_up(const char *name, uid_t user)
{
	char conf[PATH_MAX], wgconf[PATH_MAX], logpath[PATH_MAX];
	char iface[32], addr[128], prefix[16], mtu[16], marker[64], journal[PATH_MAX], statepath[PATH_MAX];
	unsigned char random[16];
	struct state_meta existing;
	char allowed[32][128];
	size_t nallowed = 0;
	pid_t pid;
	int logfd;
	char *argv_create[] = { IFCONFIG, iface, "create", NULL };
	char *argv_inet[] = { IFCONFIG, iface, "inet", addr, "netmask", NULL, NULL };
	char netmask[64];
	char *argv_mtu[] = { IFCONFIG, iface, "mtu", mtu, NULL };
	char *argv_up[] = { IFCONFIG, iface, "up", NULL };
	char *argv_wg[] = { WG, "setconf", iface, wgconf, NULL };

	if (ensure_rundir() < 0) {
		fprintf(stderr, "rundir failed\n");
		return 1;
	}
	if (runtime_path(statepath, sizeof(statepath), name, ".state") < 0 ||
	    runtime_path(journal, sizeof(journal), name, ".routes") < 0)
		return 1;
	if (path_exists(statepath) != 0) {
		if (read_meta(name, &existing) == 0 && state_healthy(name, &existing)) {
			printf("already-up %s %s\n", name, existing.iface);
			return 0;
		}
		fprintf(stderr, "stale tunnel state; run down to clean up\n");
		return 1;
	}
	if (path_exists(journal) != 0) {
		fprintf(stderr, "route journal exists; cleanup required\n");
		return 1;
	}
	if (find_conf(name, user, conf, sizeof(conf)) < 0) {
		fprintf(stderr, "config not found for %s\n", name);
		return 1;
	}
	if (runtime_path(wgconf, sizeof(wgconf), name, ".wg.conf") < 0)
		return 1;
	if (prepare_conf(conf, wgconf, addr, sizeof(addr), prefix, sizeof(prefix),
		mtu, sizeof(mtu), allowed, &nallowed, 32) < 0) {
		fprintf(stderr, "invalid or unsupported configuration\n");
		return 1;
	}
	if (pick_tun(iface, sizeof(iface)) < 0) {
		fprintf(stderr, "no free tun interface\n");
		unlink(wgconf);
		return 1;
	}
	/* NetBSD: assign inet before wireguard-go or Read returns ENETDOWN. */
	if (run_cmd(argv_create) < 0) {
		fprintf(stderr, "ifconfig create failed\n");
		unlink(wgconf);
		return 1;
	}
	arc4random_buf(random, sizeof(random));
	memcpy(marker, "ember-wg-", 9);
	for (size_t k = 0; k < sizeof(random); k++)
		snprintf(marker + 9 + k * 2, sizeof(marker) - 9 - k * 2, "%02x", random[k]);
	if (interface_marker(iface, marker, 1) != 1) {
		fprintf(stderr, "cannot mark interface %s; manual cleanup may be required\n", iface);
		(void)destroy_marked(iface, marker);
		unlink(wgconf);
		return 1;
	}
	/*
	 * Use classic inet+netmask. CIDR form on NetBSD tun(4) can leave an
	 * empty destination and break wireguard-go attach.
	 */
	{
		unsigned p = (unsigned)atoi(prefix);
		unsigned long mask = p == 0 ? 0 : p >= 32 ? 0xffffffffUL :
		    (0xffffffffUL << (32 - p));

		snprintf(netmask, sizeof(netmask), "%lu.%lu.%lu.%lu",
		    (mask >> 24) & 255, (mask >> 16) & 255,
		    (mask >> 8) & 255, mask & 255);
		argv_inet[5] = netmask;
		if (run_cmd(argv_inet) < 0) {
			fprintf(stderr, "ifconfig inet failed\n");
			(void)destroy_marked(iface, marker);
			unlink(wgconf);
			return 1;
		}
	}
	if (run_cmd(argv_mtu) < 0) {
		fprintf(stderr, "ifconfig mtu failed\n");
		(void)destroy_marked(iface, marker);
		unlink(wgconf);
		return 1;
	}
	if (run_cmd(argv_up) < 0) {
		fprintf(stderr, "ifconfig up failed\n");
		(void)destroy_marked(iface, marker);
		unlink(wgconf);
		return 1;
	}

	if (runtime_path(logpath, sizeof(logpath), name, ".log") < 0) {
		(void)destroy_marked(iface, marker);
		unlink(wgconf);
		return 1;
	}
	logfd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (logfd < 0) {
		fprintf(stderr, "cannot open tunnel log\n");
		if (destroy_marked(iface, marker) < 0)
			fprintf(stderr, "cannot remove marked interface\n");
		unlink(wgconf);
		return 1;
	}
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork failed\n");
		close(logfd);
		if (destroy_marked(iface, marker) < 0)
			fprintf(stderr, "cannot remove marked interface\n");
		unlink(logpath);
		unlink(wgconf);
		return 1;
	}
	if (pid == 0) {
		if (dup2(logfd, STDOUT_FILENO) < 0 || dup2(logfd, STDERR_FILENO) < 0)
			_exit(127);
		close(logfd);
		execl(WGO, "wireguard-go", "-f", iface, (char *)NULL);
		_exit(127);
	}
	close(logfd);
	sleep(1);
	if (write_state(name, iface, addr, marker, pid) < 0) {
		fprintf(stderr, "state write failed\n");
		if (stop_child(pid) == 0) {
			if (destroy_marked(iface, marker) < 0)
				fprintf(stderr, "cannot remove marked interface; manual cleanup required\n");
		} else {
			fprintf(stderr, "cannot stop tunnel child; interface preserved\n");
		}
		unlink(wgconf);
		return 1;
	}
	int wg_result = run_cmd(argv_wg);
	unlink(wgconf);
	if (wg_result < 0) {
		fprintf(stderr, "wg setconf failed\n");
		if (stop_child(pid) == 0)
			(void)cmd_down(name);
		return 1;
	}
	if (wg_routes_up(ROUTE, WG, iface, addr, (const char (*)[128])allowed,
	    nallowed, journal, route_runner, NULL) != 0) {
		fprintf(stderr, "route setup failed; cleanup required\n");
		if (stop_child(pid) == 0)
			(void)cmd_down(name);
		return 1;
	}

	printf("up %s %s %s/%s\n", name, iface, addr, prefix);
	return 0;
}

static int
cmd_down(const char *name)
{
	struct state_meta m;
	char journal[PATH_MAX], statepath[PATH_MAX];
	int marker, process;
	if (runtime_path(statepath, sizeof(statepath), name, ".state") < 0 ||
	    runtime_path(journal, sizeof(journal), name, ".routes") < 0)
		return 1;
	if (read_meta(name, &m) < 0) {
		if (path_exists(statepath) != 0 || path_exists(journal) != 0) {
			fprintf(stderr, "invalid or missing state; manual cleanup required\n");
			return 1;
		}
		printf("already-down %s\n", name);
		return 0;
	}
	process = state_check(&m);
	if (process < 0) {
		fprintf(stderr, "cannot inspect tunnel process; state preserved\n");
		return 1;
	}
	marker = interface_marker(m.iface, m.marker, 0);
	if (marker < 0) {
		fprintf(stderr, "interface marker mismatch or query failed\n");
		return 1;
	}
	if (wg_routes_down(ROUTE, journal, route_runner, NULL) != 0) {
		fprintf(stderr, "route cleanup failed\n");
		return 1;
	}
	if (process == 0) {
		process = state_check(&m);
		if (process < 0) {
			fprintf(stderr, "cannot inspect tunnel process; state preserved\n");
			return 1;
		}
	}
	if (process == 0) {
		/* The process may exit and its PID be reused between sysctl and kill. */
		if (kill(m.pid, SIGTERM) < 0 && errno != ESRCH) return 1;
		for (int attempt = 0; attempt < 20; attempt++) {
			struct timespec delay = { 0, 100000000 };
			process = state_check(&m);
			if (process != 0)
				break;
			while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
		}
		if (process <= 0) {
			fprintf(stderr, "tunnel process did not stop or cannot be inspected; state preserved\n");
			return 1;
		}
	}
	if (marker == 1 && (interface_marker(m.iface, m.marker, 0) != 1 ||
	    run_cmd((char *[]){ IFCONFIG, m.iface, "destroy", NULL }) < 0))
		return 1;
	clear_state(name);
	printf("down %s\n", name);
	return 0;
}

static void
emit_one(const char *name, uid_t user)
{
	char conf[PATH_MAX], iface[32];
	pid_t pid = -1;
	const char *state = "down";

	if (find_conf(name, user, conf, sizeof(conf)) < 0)
		return;
	if (read_state(name, iface, sizeof(iface), &pid) == 0) {
		struct state_meta meta;
		state = read_meta(name, &meta) == 0 && state_healthy(name, &meta) ? "up" : "failed";
	}
	else {
		char path[PATH_MAX];
		if (runtime_path(path, sizeof(path), name, ".state") == 0 && path_exists(path) != 0)
			state = "failed";
		snprintf(iface, sizeof(iface), "%s", "-");
	}
	printf("%s\t%s\t%s\t%s\n", name, state, iface, conf);
}

static void
scan_conf_dir(const char *dir, uid_t user)
{
	DIR *d;
	struct dirent *e;

	d = opendir(dir);
	if (d == NULL)
		return;
	while ((e = readdir(d)) != NULL) {
		size_t n = strlen(e->d_name);
		char stem[128], candidate[PATH_MAX], selected[PATH_MAX];

		if (n < 6 || strcmp(e->d_name + n - 5, ".conf") != 0)
			continue;
		snprintf(stem, sizeof(stem), "%.*s", (int)(n - 5), e->d_name);
		if (!valid_name(stem))
			continue;
		if (snprintf(candidate, sizeof(candidate), "%s/%s", dir, e->d_name) >= (int)sizeof(candidate) ||
		    find_conf(stem, user, selected, sizeof(selected)) < 0 || strcmp(candidate, selected) != 0)
			continue;
		emit_one(stem, user);
	}
	closedir(d);
}

static int
cmd_list(uid_t user)
{
	struct passwd *pw;
	char path[PATH_MAX];
	int i;

	scan_conf_dir(SYSCONF, user);
	pw = getpwuid(user);
	if (pw != NULL && pw->pw_dir != NULL) {
		for (i = 0; user_conf_dirs[i] != NULL; i++) {
			snprintf(path, sizeof(path), "%s/%s", pw->pw_dir,
			    user_conf_dirs[i]);
			scan_conf_dir(path, user);
		}
	}
	return 0;
}

static int
cmd_status(const char *name, uid_t user)
{
	emit_one(name, user);
	return 0;
}

static int
extract_private_key(const char *conf, char *out, size_t outsz)
{
	char *text = NULL;
	size_t len = 0;
	char *line, *save = NULL;
	char section[64] = "";
	int found = 0;

	snprintf(out, outsz, "%s", "");
	if (read_file(conf, &text, &len) < 0)
		return -1;
	for (line = strtok_r(text, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *t = line;
		char key[64], val[512];
		char *eq;

		trim_inplace(t);
		if (*t == '\0' || *t == '#' || *t == ';')
			continue;
		if (*t == '[' && t[strlen(t) - 1] == ']') {
			snprintf(section, sizeof(section), "%.*s",
			    (int)strlen(t) - 2, t + 1);
			continue;
		}
		eq = strchr(t, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';
		snprintf(key, sizeof(key), "%s", t);
		snprintf(val, sizeof(val), "%s", eq + 1);
		trim_inplace(key);
		trim_inplace(val);
		if (strcmp(section, "Interface") == 0 &&
		    strcmp(key, "PrivateKey") == 0) {
			snprintf(out, outsz, "%s", val);
			found = 1;
			break;
		}
	}
	free(text);
	return found ? 0 : -1;
}

static int
derive_pubkey(const char *priv, char *out, size_t outsz)
{
	int in_pipe[2], out_pipe[2];
	pid_t pid;
	int status;
	ssize_t n;
	char buf[128];

	snprintf(out, outsz, "%s", "");
	if (priv == NULL || *priv == '\0')
		return -1;
	if (pipe(in_pipe) < 0)
		return -1;
	if (pipe(out_pipe) < 0) {
		close(in_pipe[0]);
		close(in_pipe[1]);
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		close(in_pipe[0]);
		close(in_pipe[1]);
		close(out_pipe[0]);
		close(out_pipe[1]);
		return -1;
	}
	if (pid == 0) {
		close(in_pipe[1]);
		close(out_pipe[0]);
		dup2(in_pipe[0], STDIN_FILENO);
		dup2(out_pipe[1], STDOUT_FILENO);
		close(in_pipe[0]);
		close(out_pipe[1]);
		execl(WG, "wg", "pubkey", (char *)NULL);
		_exit(127);
	}
	close(in_pipe[0]);
	close(out_pipe[1]);
	(void)write(in_pipe[1], priv, strlen(priv));
	(void)write(in_pipe[1], "\n", 1);
	close(in_pipe[1]);
	n = read(out_pipe[0], buf, sizeof(buf) - 1);
	close(out_pipe[0]);
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0 || n <= 0)
		return -1;
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		n--;
	buf[n] = '\0';
	snprintf(out, outsz, "%s", buf);
	return 0;
}

static int
read_transfer(const char *iface, unsigned long long *rx, unsigned long long *tx)
{
	int pfd[2];
	pid_t pid;
	int status;
	char buf[4096];
	ssize_t n;
	char *line, *save = NULL;

	*rx = 0;
	*tx = 0;
	if (iface == NULL || *iface == '\0' || strcmp(iface, "-") == 0)
		return -1;
	if (pipe(pfd) < 0)
		return -1;
	pid = fork();
	if (pid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	}
	if (pid == 0) {
		close(pfd[0]);
		dup2(pfd[1], STDOUT_FILENO);
		dup2(pfd[1], STDERR_FILENO);
		close(pfd[1]);
		execl(WG, "wg", "show", iface, "transfer", (char *)NULL);
		_exit(127);
	}
	close(pfd[1]);
	n = read(pfd[0], buf, sizeof(buf) - 1);
	close(pfd[0]);
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0 || n <= 0)
		return -1;
	buf[n] = '\0';
	/* Lines: <peer-pubkey>\t<rx>\t<tx> */
	for (line = strtok_r(buf, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *p1, *p2;
		unsigned long long r = 0, t = 0;

		p1 = strchr(line, '\t');
		if (p1 == NULL)
			continue;
		*p1++ = '\0';
		p2 = strchr(p1, '\t');
		if (p2 == NULL)
			continue;
		*p2++ = '\0';
		r = strtoull(p1, NULL, 10);
		t = strtoull(p2, NULL, 10);
		*rx += r;
		*tx += t;
	}
	return 0;
}

static int
extract_address(const char *conf, char *out, size_t outsz)
{
	char *text = NULL;
	size_t len = 0;
	char *line, *save = NULL;
	char section[64] = "";

	snprintf(out, outsz, "%s", "");
	if (read_file(conf, &text, &len) < 0)
		return -1;
	for (line = strtok_r(text, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *t = line;
		char key[64], val[512];
		char *eq;

		trim_inplace(t);
		if (*t == '\0' || *t == '#' || *t == ';')
			continue;
		if (*t == '[' && t[strlen(t) - 1] == ']') {
			snprintf(section, sizeof(section), "%.*s",
			    (int)strlen(t) - 2, t + 1);
			continue;
		}
		eq = strchr(t, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';
		snprintf(key, sizeof(key), "%s", t);
		snprintf(val, sizeof(val), "%s", eq + 1);
		trim_inplace(key);
		trim_inplace(val);
		if (strcmp(section, "Interface") == 0 &&
		    strcmp(key, "Address") == 0) {
			snprintf(out, outsz, "%s", val);
			free(text);
			return 0;
		}
	}
	free(text);
	return -1;
}

static int
cmd_detail(const char *name, uid_t user)
{
	char conf[PATH_MAX], iface[32], priv[128], pub[128], addr[256];
	pid_t pid = -1;
	const char *state = "down";
	unsigned long long rx = 0, tx = 0;

	if (find_conf(name, user, conf, sizeof(conf)) < 0) {
		fprintf(stderr, "config not found for %s\n", name);
		return 1;
	}
	if (read_state(name, iface, sizeof(iface), &pid) == 0) {
		struct state_meta meta;
		state = read_meta(name, &meta) == 0 && state_healthy(name, &meta) ? "up" : "failed";
	}
	else {
		char path[PATH_MAX];
		if (runtime_path(path, sizeof(path), name, ".state") == 0 && path_exists(path) != 0)
			state = "failed";
		snprintf(iface, sizeof(iface), "%s", "-");
	}
	printf("%s\t%s\t%s\t%s\n", name, state, iface, conf);

	snprintf(priv, sizeof(priv), "%s", "");
	snprintf(pub, sizeof(pub), "%s", "");
	if (extract_private_key(conf, priv, sizeof(priv)) == 0)
		(void)derive_pubkey(priv, pub, sizeof(pub));
	printf("public_key\t%s\n", pub[0] ? pub : "(unknown)");

	snprintf(addr, sizeof(addr), "%s", "");
	(void)extract_address(conf, addr, sizeof(addr));
	printf("address\t%s\n", addr[0] ? addr : "(none)");

	if (strcmp(state, "up") == 0)
		(void)read_transfer(iface, &rx, &tx);
	printf("transfer_rx\t%llu\n", rx);
	printf("transfer_tx\t%llu\n", tx);
	return 0;
}

int
main(int argc, char **argv)
{
	uid_t user;
	const char *cmd;

	user = getuid();
	if (seteuid(0) < 0) {
		/* Allow non-setuid builds for packaging smoke tests. */
		if (geteuid() != 0) {
			fprintf(stderr, "ember-wireguard-ctl needs root\n");
			return 1;
		}
	}
	drop_env();
	umask(077);
	if (argc < 2) {
		fprintf(stderr,
		    "usage: ember-wireguard-ctl list|status|detail|up|down [name]\n");
		return 2;
	}
	cmd = argv[1];
	if (strcmp(cmd, "list") == 0)
		return cmd_list(user);
	if (argc < 3 || !valid_name(argv[2])) {
		fprintf(stderr, "invalid tunnel name\n");
		return 2;
	}
	if (strcmp(cmd, "status") == 0)
		return cmd_status(argv[2], user);
	if (strcmp(cmd, "detail") == 0)
		return cmd_detail(argv[2], user);
	if (strcmp(cmd, "up") == 0 || strcmp(cmd, "down") == 0) {
		if (!may_change_tunnel(user)) {
			fprintf(stderr, "Permission denied: tunnel changes require "
			    "root or membership in the " OPERATOR_GROUP " group.\n");
			return 1;
		}
#ifdef __NetBSD__
		int lockfd, result;
		lockfd = runtime_lock();
		if (lockfd < 0) {
			fprintf(stderr, "runtime lock failed\n");
			return 1;
		}
		result = strcmp(cmd, "up") == 0 ? cmd_up(argv[2], user) :
		    cmd_down(argv[2]);
		close(lockfd);
		return result;
#else
		fprintf(stderr, "tunnel operations require NetBSD\n");
		return 1;
#endif
	}
	fprintf(stderr, "unknown command\n");
	return 2;
}
