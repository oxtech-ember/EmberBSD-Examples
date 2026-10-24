/* SPDX-License-Identifier: BSD-2-Clause */
#include "iio_rx.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Protocol described by libiio v0.25 iiod-client.c. No upstream code copied. */
static const char xml[] =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<!DOCTYPE context ["
"<!ELEMENT context (context-attribute*,device*)>"
"<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>"
"<!ELEMENT context-attribute EMPTY>"
"<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>"
"<!ELEMENT device (channel*,attribute*)>"
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ELEMENT channel (scan-element?,attribute*)>"
"<!ATTLIST channel id CDATA #REQUIRED name CDATA #IMPLIED type (input|output) #REQUIRED>"
"<!ELEMENT scan-element EMPTY>"
"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED>"
"<!ELEMENT attribute EMPTY>"
"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>]>"
"<context name=\"local\" description=\"EmberBSD Pluto native RX\">"
"<context-attribute name=\"hw_model\" value=\"EmberBSD Pluto RX\"/>"
"<device id=\"iio:device0\" name=\"ad9361-phy\">"
"<channel id=\"voltage0\" type=\"input\">"
"<attribute name=\"sampling_frequency\"/><attribute name=\"rf_bandwidth\"/>"
"<attribute name=\"rf_port_select\"/><attribute name=\"gain_control_mode\"/>"
"<attribute name=\"hardwaregain\"/><attribute name=\"filter_fir_en\"/></channel>"
"<channel id=\"altvoltage0\" name=\"RX_LO\" type=\"output\">"
"<attribute name=\"frequency\"/><attribute name=\"powerdown\"/></channel>"
"<channel id=\"altvoltage1\" name=\"TX_LO\" type=\"output\">"
"<attribute name=\"powerdown\"/></channel>"
"<attribute name=\"in_out_voltage_filter_fir_en\"/>"
"<attribute name=\"filter_fir_config\"/></device>"
"<device id=\"iio:device1\" name=\"cf-ad9361-lpc\">"
"<channel id=\"voltage0\" type=\"input\">"
"<scan-element index=\"0\" format=\"le:s12/16&gt;&gt;0\"/>"
"<attribute name=\"sampling_frequency\"/></channel>"
"<channel id=\"voltage1\" type=\"input\">"
"<scan-element index=\"1\" format=\"le:s12/16&gt;&gt;0\"/>"
"<attribute name=\"sampling_frequency\"/></channel>"
"<attribute name=\"sampling_frequency\"/>"
"<attribute name=\"rx_discontinuities\"/><attribute name=\"rx_dropped_blocks\"/>"
"<attribute name=\"rx_loss_unknown\"/></device></context>";

const char *
rx_xml(void)
{
	return xml;
}

void
rx_init(struct rx_service *s, const struct rx_ops *o, void *a)
{
	memset(s, 0, sizeof(*s));
	s->ops = *o;
	s->arg = a;
}

static void
state_lock(struct rx_service *s)
{
	if (s->ops.state_lock) s->ops.state_lock(s->arg);
}

static void
state_unlock(struct rx_service *s)
{
	if (s->ops.state_unlock) s->ops.state_unlock(s->arg);
}

static int
owns(struct rx_service *s, unsigned p)
{
	int e;
	state_lock(s);
	e = !s->active ? -EBADF : s->owner != (int)p ? -EBUSY : 0;
	state_unlock(s);
	return e;
}

static int
number(struct rx_service *s, unsigned p, long n)
{
	char b[32];
	/* IIOD uses the Linux errno wire numbers, not the BSD errno ABI. */
	if (n == -EOVERFLOW) n = -75;
	else if (n == -ETIMEDOUT) n = -110;
	else if (n == -EOPNOTSUPP) n = -95;
	else if (n == -ECANCELED) n = -125;
	else if (n == -ESTALE) n = -116;
	else if (n == -EPROTO) n = -71;
	else if (n == -ENODATA) n = -61;
	else if (n == -ENOSYS) n = -38;
	else if (n == -EAGAIN) n = -11;
	int len = snprintf(b, sizeof(b), "%ld\n", n);
	return s->ops.send(s->arg, p, b, (size_t)len);
}

static int
value(struct rx_service *s, unsigned p, const char *b, size_t n)
{
	int e = number(s, p, (long)n);
	if (e == 0) e = s->ops.send(s->arg, p, b, n);
	if (e == 0) e = s->ops.send(s->arg, p, "\n", 1);
	return e;
}

static int
decimal(const char *b, unsigned long *v)
{
	char *end;
	if (*b == '\0') return -EINVAL;
	for (const char *q = b; *q; q++)
		if (*q < '0' || *q > '9') return -EINVAL;
	errno = 0;
	*v = strtoul(b, &end, 10);
	return errno || *end ? -EINVAL : 0;
}

static int
attribute(struct rx_service *s, unsigned p, char **t, size_t nt, int write)
{
	char b[RX_PAYLOAD_MAX + 1], ch[64] = "";
	const char *a;
	int e;
	if (nt == (write ? 4U : 3U)) {
		a = t[2];
	} else if (nt == (write ? 6U : 5U) &&
	    (!strcmp(t[2], "INPUT") || !strcmp(t[2], "OUTPUT"))) {
		if (strlen(t[3]) > 48) return number(s, p, -EINVAL);
		(void)snprintf(ch, sizeof(ch), "%s:%s", t[2], t[3]);
		a = t[4];
	} else return number(s, p, -EINVAL);
	if (write) {
		const struct rx_pipe *pp = &s->pipe[p];
		memcpy(b, pp->payload, pp->pending);
		b[pp->pending] = '\0';
		/* libiio string writes include their terminating NUL. */
		if (memchr(b, '\0', pp->pending > 0 ? pp->pending - 1 : 0))
			return number(s, p, -EINVAL);
	}
	e = s->ops.attr(s->arg, t[1], ch, a, b, sizeof(b), write);
	if (e < 0) return number(s, p, e);
	if (!write && (size_t)e >= sizeof(b)) return number(s, p, -EOVERFLOW);
	return write ? number(s, p, (long)s->pipe[p].pending) :
	    value(s, p, b, (size_t)e);
}

static int
command(struct rx_service *s, unsigned p, int payload)
{
	char line[RX_COMMAND_MAX], *t[8], *save, *q;
	size_t nt = 0;
	unsigned long n;
	int e;
	strcpy(line, s->pipe[p].line);
	for (q = strtok_r(line, " \t\r", &save); q;
	    q = strtok_r(NULL, " \t\r", &save)) {
		if (nt == 8) {
			if (!strcmp(t[0], "WRITE")) s->pipe[p].poisoned = 1;
			return number(s, p, -EINVAL);
		}
		t[nt++] = q;
	}
	if (nt == 0) return number(s, p, -EINVAL);
	if (!strcmp(t[0], "PRINT") && nt == 1)
		return value(s, p, xml, sizeof(xml) - 1);
	if (!strcmp(t[0], "VERSION") && nt == 1)
		return s->ops.send(s->arg, p, "0.25.emberrx\n", 13);
	if (!strcmp(t[0], "TIMEOUT") && nt == 2 &&
	    decimal(t[1], &n) == 0 && n > 0 && n <= 60000)
		return number(s, p, s->ops.settings(s->arg, (unsigned)n, 0));
	if (!strcmp(t[0], "SET") && nt == 4 &&
	    !strcmp(t[1], "iio:device1") && !strcmp(t[2], "BUFFERS_COUNT") &&
	    decimal(t[3], &n) == 0 && n >= 3 && n <= 16)
		return number(s, p, s->ops.settings(s->arg, 0, (unsigned)n));
	if (!strcmp(t[0], "READ")) return attribute(s, p, t, nt, 0);
	if (!strcmp(t[0], "WRITE")) {
		if (nt < 4 || decimal(t[nt - 1], &n)) {
			s->pipe[p].poisoned = 1;
			return number(s, p, -EINVAL);
		}
		if (n == 0 || n > RX_PAYLOAD_MAX) {
			s->pipe[p].poisoned = 1;
			return number(s, p, -E2BIG);
		}
		if (payload) return attribute(s, p, t, nt, 1);
		s->pipe[p].pending = n;
		return 0;
	}
	if (!strcmp(t[0], "OPEN") && nt == 4 &&
	    !strcmp(t[1], "iio:device1") && !decimal(t[2], &n) &&
	    n >= 2 && n <= 1048576 && !(n & 1) && !strcmp(t[3], "00000003")) {
		state_lock(s);
		if (s->active || s->closing) { state_unlock(s); return number(s, p, -EBUSY); }
		s->active = 1; s->owner = (int)p;
		state_unlock(s);
		e = s->ops.start(s->arg, (unsigned)n);
		if (e) { state_lock(s); s->active = 0; state_unlock(s); }
		return number(s, p, e);
	}
	if (!strcmp(t[0], "CLOSE") && nt == 2 && !strcmp(t[1], "iio:device1")) {
		if ((e = owns(s, p)) != 0) return number(s, p, e);
		e = rx_close_pipe(s, p);
		return number(s, p, e);
	}
	if (!strcmp(t[0], "READBUF") && nt == 3 && !strcmp(t[1], "iio:device1") &&
	    !decimal(t[2], &n) && n > 0 && n <= 4194304 && !(n & 3)) {
		unsigned char b[RX_CHUNK_MAX];
		int first = 1;
		if ((e = owns(s, p)) != 0) return number(s, p, e);
		if (s->ops.begin_read && (e = s->ops.begin_read(s->arg)) != 0)
			return number(s, p, e);
		while (n > 0) {
			size_t want = n < sizeof(b) ? n : sizeof(b);
			ssize_t got = s->ops.read(s->arg, b, want);
			if (got <= 0 || (size_t)got > want || (got & 3)) {
				if (s->ops.end_read) s->ops.end_read(s->arg);
				(void)rx_close_pipe(s, p);
				return number(s, p, got < 0 ? got : -EIO);
			}
			e = number(s, p, got);
			if (!e && first) e = s->ops.send(s->arg, p, "00000003\n", 9);
			if (!e) e = s->ops.send(s->arg, p, b, (size_t)got);
			if (e) { if (s->ops.end_read) s->ops.end_read(s->arg); return e; }
			first = 0; n -= (size_t)got;
		}
		if (s->ops.end_read) s->ops.end_read(s->arg);
		return 0;
	}
	return number(s, p, -EINVAL);
}

int
rx_feed(struct rx_service *s, unsigned p, const void *data, size_t n)
{
	const unsigned char *b = data;
	struct rx_pipe *pp;
	if (p > 1 || (data == NULL && n)) return -EINVAL;
	pp = &s->pipe[p];
	if (pp->poisoned) return -EPROTO;
	for (size_t i = 0; i < n; i++) {
		int e;
		if (pp->pending) {
			pp->payload[pp->received++] = (char)b[i];
			if (pp->received != pp->pending) continue;
			e = command(s, p, 1);
			pp->used = pp->pending = pp->received = 0;
		} else if (b[i] == '\n') {
			pp->line[pp->used] = '\0';
			e = command(s, p, 0);
			if (!pp->pending) pp->used = 0;
		} else {
			if (b[i] == 0 || pp->used == sizeof(pp->line) - 1) {
				pp->poisoned = 1;
				return -EMSGSIZE;
			}
			pp->line[pp->used++] = (char)b[i];
			continue;
		}
		if (e) return e;
		if (pp->poisoned && i + 1 < n) return -EPROTO;
	}
	return 0;
}

int
rx_disconnect(struct rx_service *s)
{
	int active;
	state_lock(s); active = s->active;
	s->active = 0;
	state_unlock(s);
	int e = active ? s->ops.stop(s->arg) : 0;
	memset(s->pipe, 0, sizeof(s->pipe));
	return e;
}

int
rx_close_pipe(struct rx_service *s, unsigned p)
{
	int e = 0;
	if (p > 1) return -EINVAL;
	state_lock(s);
	int active = s->active && s->owner == (int)p;
	if (active)
		s->active = 0;
	state_unlock(s);
	if (active) e = s->ops.stop(s->arg);
	memset(&s->pipe[p], 0, sizeof(s->pipe[p]));
	return e;
}
