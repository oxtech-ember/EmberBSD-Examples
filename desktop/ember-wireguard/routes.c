/* SPDX-License-Identifier: BSD-2-Clause */
/* IPv4 route policy over the system route(8) mechanism. */
#include "routes.h"

#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ROUTES 96
#define CAPTURE_SIZE 16384

struct route {
	char dest[INET_ADDRSTRLEN];
	char gateway[INET_ADDRSTRLEN];
	unsigned prefix;
	int direct;
	int applied;
};

struct current_route {
	struct route route;
	char iface[32];
	char local[INET_ADDRSTRLEN];
};

static int
ipv4(const char *s)
{
	struct in_addr a;
	return inet_pton(AF_INET, s, &a) == 1;
}

static int
prefix(const char *s, struct route *r)
{
	char text[128], *slash, *end;
	unsigned long bits;
	struct in_addr a;
	uint32_t mask;

	if (strlen(s) >= sizeof(text))
		return -1;
	memcpy(text, s, strlen(s) + 1);
	slash = strchr(text, '/');
	bits = 32;
	if (slash != NULL) {
		*slash++ = '\0';
		if (*slash < '0' || *slash > '9')
			return -1;
		errno = 0;
		bits = strtoul(slash, &end, 10);
		if (errno != 0 || *end != '\0' || bits > 32)
			return -1;
	}
	if (inet_pton(AF_INET, text, &a) != 1)
		return -1;
	mask = bits == 0 ? 0 : UINT32_MAX << (32 - bits);
	a.s_addr = htonl(ntohl(a.s_addr) & mask);
	if (inet_ntop(AF_INET, &a, r->dest, sizeof(r->dest)) == NULL)
		return -1;
	r->prefix = (unsigned)bits;
	return 0;
}

static int
contains(const struct route *r, const char *address)
{
	struct in_addr a, d;
	uint32_t mask = r->prefix == 0 ? 0 : UINT32_MAX << (32 - r->prefix);
	if (inet_pton(AF_INET, address, &a) != 1 ||
	    inet_pton(AF_INET, r->dest, &d) != 1)
		return 0;
	return (ntohl(a.s_addr) & mask) == ntohl(d.s_addr);
}

static char *
trim(char *s)
{
	char *end;
	while (*s == ' ' || *s == '\t')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
		*--end = '\0';
	return s;
}

static int
current(const char *tool, const char *address, int exact_prefix,
    wg_route_runner run, void *ctx,
    struct current_route *out)
{
	char exact_bits[4];
	const char *argv[9] = { tool, "-n", "get", "-inet", address, NULL };
	char text[CAPTURE_SIZE], *line, *save = NULL, dest[32] = "", mask[32] = "";
	int host = 0, gateway = 0;
	struct in_addr m;
	uint32_t v;
	unsigned bits = 0;

	memset(out, 0, sizeof(*out));
	if (exact_prefix >= 0) {
		snprintf(exact_bits, sizeof(exact_bits), "%d", exact_prefix);
		argv[5] = "-prefixlen";
		argv[6] = exact_bits;
	}
	text[0] = '\0';
	if (run(ctx, argv, text, sizeof(text)) != 0)
		return strstr(text, "not in table") != NULL ? 1 : -1;
	for (line = strtok_r(text, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *key = trim(line), *value = strchr(key, ':');
		if (value == NULL)
			continue;
		*value++ = '\0';
		value = trim(value);
		if (strcmp(key, "destination") == 0)
			snprintf(dest, sizeof(dest), "%s", value);
		else if (strcmp(key, "mask") == 0)
			snprintf(mask, sizeof(mask), "%s", value);
		else if (strcmp(key, "gateway") == 0 && ipv4(value))
			snprintf(out->route.gateway, sizeof(out->route.gateway), "%s", value);
		else if (strcmp(key, "local addr") == 0 && ipv4(value))
			snprintf(out->local, sizeof(out->local), "%s", value);
		else if (strcmp(key, "interface") == 0)
			snprintf(out->iface, sizeof(out->iface), "%s", value);
		else if (strcmp(key, "flags") == 0) {
			host = strstr(value, "HOST") != NULL;
			gateway = strstr(value, "GATEWAY") != NULL;
		}
	}
	if (strcmp(dest, "default") == 0)
		strcpy(dest, "0.0.0.0");
	if (strcmp(mask, "default") == 0) {
		bits = 0;
	} else if (host) {
		bits = 32;
	} else {
		if (inet_pton(AF_INET, mask, &m) != 1)
			return -1;
		v = ntohl(m.s_addr);
		while (v & UINT32_C(0x80000000)) {
			bits++;
			v <<= 1;
		}
		if (v != 0)
			return -1;
	}
	if (!ipv4(dest) || !out->iface[0])
		return -1;
	strcpy(out->route.dest, dest);
	out->route.prefix = bits;
	out->route.direct = !gateway;
	if (!gateway && ipv4(out->local))
		strcpy(out->route.gateway, out->local);
	if (!ipv4(out->route.gateway))
		return -1;
	/* NetBSD can answer a /32 lookup with a covering network route. */
	if (exact_prefix >= 0 && bits != (unsigned)exact_prefix)
		return exact_prefix == 32 && bits < 32 &&
		    contains(&out->route, address) ? 1 : -1;
	return 0;
}

static int
same_prefix(const struct route *a, const struct route *b)
{
	return a->prefix == b->prefix && strcmp(a->dest, b->dest) == 0;
}

static int
same_route(const struct route *a, const struct route *b)
{
	return same_prefix(a, b) && a->direct == b->direct &&
	    strcmp(a->gateway, b->gateway) == 0;
}

static int
command(const char *tool, const char *action, const struct route *r,
    wg_route_runner run, void *ctx)
{
	char bits[4];
	const char *argv[10] = { tool, action, "-inet", r->dest, "-prefixlen", bits };
	snprintf(bits, sizeof(bits), "%u", r->prefix);
	if (r->direct) {
		argv[6] = "-interface";
		argv[7] = r->gateway;
	} else {
		argv[6] = r->gateway;
	}
	return run(ctx, argv, NULL, 0);
}

static int
journal_write(const char *path, const struct route *routes, size_t n)
{
	char temp[PATH_MAX], parent[PATH_MAX], *slash;
	int fd, dirfd, rc = -1;
	if (snprintf(temp, sizeof(temp), "%s.new.XXXXXX", path) >= (int)sizeof(temp) ||
	    strlen(path) >= sizeof(parent))
		return -1;
	strcpy(parent, path);
	slash = strrchr(parent, '/');
	if (slash == NULL)
		return -1;
	*slash = '\0';
	dirfd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0)
		return -1;
	fd = mkstemp(temp);
	if (fd < 0) {
		close(dirfd);
		return -1;
	}
	if (fchmod(fd, 0600) != 0)
		goto out;
	for (size_t i = 0; i < n; i++)
		if (dprintf(fd, "%s/%u %s %d %d\n", routes[i].dest,
		    routes[i].prefix, routes[i].gateway, routes[i].direct,
		    routes[i].applied) < 0)
			goto out;
	if (fsync(fd) != 0)
		goto out;
	if (close(fd) != 0) {
		fd = -1;
		goto out;
	}
	fd = -1;
	if (rename(temp, path) != 0 || fsync(dirfd) != 0)
		goto out;
	rc = 0;
out:
	if (fd >= 0)
		close(fd);
	if (rc != 0)
		unlink(temp);
	close(dirfd);
	return rc;
}

static int
undo(const char *tool, struct route *routes, size_t *n,
    wg_route_runner run, void *ctx)
{
	int failed = 0;
	for (size_t i = *n; i > 0; i--) {
		struct current_route now;
		struct route *r = &routes[i - 1];
		int found = current(tool, r->dest, (int)r->prefix, run, ctx, &now);
		if (found < 0) {
			fprintf(stderr, "cannot inspect owned route %s/%u\n", r->dest, r->prefix);
			failed = 1;
			continue;
		}
		if (found == 0 && same_route(r, &now.route) && !r->applied) {
			fprintf(stderr, "route %s/%u has an incomplete operation; ownership must be resolved\n",
			    r->dest, r->prefix);
			failed = 1;
			continue;
		}
		/* Never remove a route replaced by another administrator. */
		if (found == 0 && same_route(r, &now.route) &&
		    command(tool, "delete", r, run, ctx) != 0) {
			fprintf(stderr, "cannot remove owned route %s/%u\n", r->dest, r->prefix);
			failed = 1;
			continue;
		}
		memmove(r, r + 1, (*n - i) * sizeof(*r));
		(*n)--;
	}
	return failed ? -1 : 0;
}

static int
read_journal(const char *path, struct route *owned, size_t *n)
{
	struct stat st;
	char line[128];
	int fd;
	FILE *f;
	*n = 0;
	fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return errno == ENOENT ? 1 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 077) != 0 || st.st_size > 16384) {
		close(fd);
		return -1;
	}
	f = fdopen(fd, "r");
	if (f == NULL) {
		close(fd);
		return -1;
	}
	while (fgets(line, sizeof(line), f) != NULL) {
		char cidr[64], gateway[32], extra;
		int direct, applied;
		if (*n == MAX_ROUTES || strchr(line, '\n') == NULL ||
		    sscanf(line, "%63s %31s %d %d %c", cidr, gateway, &direct, &applied, &extra) != 4 ||
		    prefix(cidr, &owned[*n]) != 0 || !ipv4(gateway) ||
		    (direct != 0 && direct != 1) || (applied != 0 && applied != 1)) {
			fclose(f);
			return -1;
		}
		strcpy(owned[*n].gateway, gateway);
		owned[*n].applied = applied;
		owned[(*n)++].direct = direct;
	}
	if (ferror(f)) {
		fclose(f);
		return -1;
	}
	return fclose(f) == 0 ? 0 : -1;
}

/* Never borrow a route another managed tunnel may remove on disconnect. */
static int
owned_elsewhere(const char *journal, const struct route *route)
{
	char dir[PATH_MAX], path[PATH_MAX], *slash;
	struct dirent *entry;
	struct route routes[MAX_ROUTES];
	size_t n;
	DIR *d;
	int result = 0;
	if (strlen(journal) >= sizeof(dir))
		return -1;
	strcpy(dir, journal);
	slash = strrchr(dir, '/');
	if (slash == NULL)
		return -1;
	*slash = '\0';
	d = opendir(dir);
	if (d == NULL)
		return -1;
	while ((entry = readdir(d)) != NULL) {
		size_t len = strlen(entry->d_name);
		if (len < 7 || strcmp(entry->d_name + len - 7, ".routes") != 0)
			continue;
		if (snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) >= (int)sizeof(path)) {
			result = -1;
			break;
		}
		if (strcmp(path, journal) == 0)
			continue;
		if (read_journal(path, routes, &n) != 0) {
			result = -1;
			break;
		}
		for (size_t i = 0; i < n; i++) {
			if (same_prefix(route, &routes[i])) {
				result = 1;
				break;
			}
		}
		if (result != 0)
			break;
	}
	closedir(d);
	return result;
}

static int
add(const char *tool, const struct route *r, struct route *owned, size_t *n,
    const char *journal, wg_route_runner run, void *ctx)
{
	struct current_route now;
	int found = current(tool, r->dest, (int)r->prefix, run, ctx, &now);
	if (found < 0) {
		fprintf(stderr, "cannot inspect route %s/%u\n", r->dest, r->prefix);
		return -1;
	}
	if (found == 0 &&
	    same_prefix(r, &now.route)) {
		if (same_route(r, &now.route) && owned_elsewhere(journal, r) == 0)
			return 0; /* Existing routes remain owned by their creator. */
		fprintf(stderr, "conflicting route %s/%u\n", r->dest, r->prefix);
		return -1;
	}
	if (*n == MAX_ROUTES)
		return -1;
	owned[*n] = *r;
	owned[(*n)++].applied = 0;
	/* Persist intent first. An ambiguous interrupted add is never auto-deleted. */
	if (journal_write(journal, owned, *n) != 0) {
		(*n)--;
		return -1;
	}
	if (command(tool, "add", r, run, ctx) != 0) {
		(*n)--;
		(void)journal_write(journal, owned, *n);
		fprintf(stderr, "cannot add route %s/%u\n", r->dest, r->prefix);
		return -1;
	}
	owned[*n - 1].applied = 1;
	return journal_write(journal, owned, *n);
}

int
wg_routes_up(const char *tool, const char *wg, const char *iface, const char *addr,
    const char allowed[][128], size_t count, const char *journal,
    wg_route_runner run, void *ctx)
{
	struct route wanted[32], owned[MAX_ROUTES], endpoints[32];
	size_t n = 0, ne = 0;
	char text[CAPTURE_SIZE], *line, *save = NULL;
	const char *argv[] = { wg, "show", iface, "endpoints", NULL };
	int fd = -1, full = 0, rc = -1;

	if (!ipv4(addr) || count > 32)
		return -1;
	memset(wanted, 0, sizeof(wanted));
	for (size_t i = 0; i < count; i++) {
		if (prefix(allowed[i], &wanted[i]) != 0) {
			fprintf(stderr, "routes require valid IPv4 AllowedIPs\n");
			return -1;
		}
		strcpy(wanted[i].gateway, addr);
		wanted[i].direct = 1;
		full |= wanted[i].prefix == 0;
	}
	/* Capture resolved endpoints before installing any covering routes. */
	if (run(ctx, argv, text, sizeof(text)) != 0)
		return -1;
	for (line = strtok_r(text, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		struct current_route before;
		char *endpoint = strchr(line, '\t'), *port;
		int covered = 0, duplicate = 0;
		if (endpoint == NULL)
			return -1;
		endpoint++;
		if (strcmp(endpoint, "(none)") == 0)
			continue;
		port = strrchr(endpoint, ':');
		if (port == NULL)
			return -1;
		*port = '\0';
		if (!ipv4(endpoint)) {
			fprintf(stderr, "IPv6 endpoints are not supported by this backend\n");
			return -1;
		}
		for (size_t i = 0; i < count; i++)
			covered |= contains(&wanted[i], endpoint);
		if (!covered)
			continue;
		for (size_t i = 0; i < ne; i++)
			duplicate |= strcmp(endpoints[i].dest, endpoint) == 0;
		if (duplicate)
			continue;
		if (ne == 32 || current(tool, endpoint, -1, run, ctx, &before) != 0 ||
		    strcmp(before.iface, iface) == 0) {
			fprintf(stderr, "cannot preserve endpoint route outside the tunnel\n");
			return -1;
		}
		endpoints[ne] = before.route;
		strcpy(endpoints[ne].dest, endpoint);
		endpoints[ne++].prefix = 32;
	}
	if (full && ne == 0) {
		fprintf(stderr, "a default tunnel requires a resolved IPv4 endpoint\n");
		return -1;
	}
	fd = open(journal, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) {
		perror("route journal");
		return -1;
	}
	if (close(fd) != 0)
		return -1;
	for (size_t i = 0; i < ne; i++)
		if (add(tool, &endpoints[i], owned, &n, journal, run, ctx) != 0)
			goto out;
	for (size_t i = 0; i < count; i++) {
		int endpoint_host = 0;
		for (size_t j = 0; j < ne; j++)
			endpoint_host |= same_prefix(&wanted[i], &endpoints[j]);
		if (endpoint_host)
			continue;
		if (wanted[i].prefix == 0) {
			/* Keep the physical default: endpoint escapes plus two /1 routes. */
			struct route half = wanted[i];
			half.prefix = 1;
			if (add(tool, &half, owned, &n, journal, run, ctx) != 0)
				goto out;
			strcpy(half.dest, "128.0.0.0");
			if (add(tool, &half, owned, &n, journal, run, ctx) != 0)
				goto out;
		} else if (add(tool, &wanted[i], owned, &n, journal, run, ctx) != 0) {
			goto out;
		}
	}
	rc = 0;
out:
	if (rc != 0) {
		(void)undo(tool, owned, &n, run, ctx);
		if (journal_write(journal, owned, n) != 0) {
			fprintf(stderr, "cannot persist incomplete route cleanup\n");
			return -1;
		}
	}
	if (rc != 0 && n == 0)
		(void)unlink(journal);
	return rc;
}

int
wg_routes_down(const char *tool, const char *journal, wg_route_runner run, void *ctx)
{
	struct route owned[MAX_ROUTES];
	size_t n = 0;
	int rc = read_journal(journal, owned, &n);
	if (rc != 0)
		return rc == 1 ? 0 : -1;
	rc = undo(tool, owned, &n, run, ctx);
	if (journal_write(journal, owned, n) != 0)
		rc = -1;
	if (rc == 0 && unlink(journal) != 0)
		rc = -1;
	return rc;
}
