/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */

/* A bounded localhost-only fault relay. This program does not encode RTP. */
#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_PACKET 65535U
#define MAX_DROPS 1024U
#define MAX_PACKETS 1000000U
#define MAX_CAPTURE_BYTES UINT64_C(536870912)
#define F_RX 1U
#define F_TX 2U
#define F_DROP 4U
#define F_DUP 8U
#define F_HELD 16U
#define F_REORDER 32U
#define F_BLACKOUT 64U
#define F_INVALID 128U

struct options {
	unsigned int listen_port, forward_port;
	uint32_t drops[MAX_DROPS], drop_count, drop_fu, drop_vcl_fu, duplicate, reorder;
	uint32_t blackout_first, blackout_last;
	uint32_t blackout_ms_first, blackout_ms_last;
	uint32_t max_ms, idle_ms;
	int blackout_ms_enabled;
	const char *rx_path, *tx_path, *stats_path, *ready_path;
};

struct counters {
	int receive_buffer_bytes;
	uint32_t received, forwarded, dropped, malformed, truncated;
	uint32_t middle_fu, duplicate_sends, reorder_holds, blackout_drops;
	uint32_t middle_vcl_fu, selected_fu_drops;
	int selected_fu_type;
	uint32_t held_at_end;
};

static const char *failure;
static uint64_t capture_bytes;

static void
usage(void)
{
	fputs("Usage: rtp-fault-relay --listen PORT --forward PORT --capture RX.rtp\n"
	    "  --forward-capture TX.rtp --stats FILE --max-ms N --idle-ms N\n"
	    "  [--ready-file FILE] [--drop A,B,...] [--drop-fu N] [--drop-vcl-fu N]\n"
	    "  [--duplicate N] [--reorder N] [--blackout A:B]\n"
	    "  [--blackout-ms A:B]\n"
	    "Ordinals are 1-based received datagrams. drop-fu counts middle FU\n"
	    "packets. drop-vcl-fu counts only middle FUs of NAL types 0..31.\n"
	    "Blackout ranges are inclusive; milliseconds start at the\n"
	    "first received packet. All sockets bind or send to 127.0.0.1.\n"
	    "Capture: H265RTP1, then BE u32 length,ordinal,flags,reserved=0;\n"
	    "BE u64 elapsed microseconds and the exact datagram. RX=1,TX=2,\n"
	    "DROP=4,DUP=8,HELD=16,REORDER=32,BLACKOUT=64,INVALID=128.\n"
	    "TX means the local kernel accepted sendto, not remote delivery.\n", stderr);
}

static int
number(const char *s, uint32_t min, uint32_t max, uint32_t *result)
{
	uint32_t value = 0;

	if (*s == '\0')
		return -1;
	for (; *s != '\0'; s++) {
		uint32_t digit;
		if (*s < '0' || *s > '9')
			return -1;
		digit = (uint32_t)(*s - '0');
		if (digit > max || value > (max - digit) / 10U)
			return -1;
		value = value * 10U + digit;
	}
	if (value < min || value > max)
		return -1;
	*result = value;
	return 0;
}

static int
range(const char *s, uint32_t max, uint32_t *first, uint32_t *last)
{
	char left[16];
	const char *sep = strchr(s, ':');
	size_t len;

	if (sep == NULL || strchr(sep + 1, ':') != NULL)
		return -1;
	len = (size_t)(sep - s);
	if (len == 0 || len >= sizeof(left))
		return -1;
	memcpy(left, s, len);
	left[len] = '\0';
	if (number(left, 0, max, first) != 0 ||
	    number(sep + 1, 0, max, last) != 0 || *last < *first)
		return -1;
	return 0;
}

static int
drop_list(struct options *o, const char *s)
{
	char entry[16];
	const char *end;
	size_t len;
	uint32_t value, i;

	for (;;) {
		end = strchr(s, ',');
		len = end == NULL ? strlen(s) : (size_t)(end - s);
		if (len == 0 || len >= sizeof(entry) || o->drop_count == MAX_DROPS)
			return -1;
		memcpy(entry, s, len);
		entry[len] = '\0';
		if (number(entry, 1, MAX_PACKETS, &value) != 0)
			return -1;
		for (i = 0; i < o->drop_count; i++)
			if (o->drops[i] == value)
				return -1;
		o->drops[o->drop_count++] = value;
		if (end == NULL)
			return 0;
		s = end + 1;
	}
}

static int
options(int argc, char **argv, struct options *o)
{
	uint32_t value;
	unsigned int seen = 0, bit;
	const char *name, *arg;
	int i;

	memset(o, 0, sizeof(*o));
	for (i = 1; i < argc; i += 2) {
		if (i + 1 >= argc)
			return -1;
		name = argv[i];
		arg = argv[i + 1];
		if (strcmp(name, "--listen") == 0) bit = 1U << 0;
		else if (strcmp(name, "--forward") == 0) bit = 1U << 1;
		else if (strcmp(name, "--capture") == 0) bit = 1U << 2;
		else if (strcmp(name, "--forward-capture") == 0) bit = 1U << 3;
		else if (strcmp(name, "--stats") == 0) bit = 1U << 4;
		else if (strcmp(name, "--max-ms") == 0) bit = 1U << 5;
		else if (strcmp(name, "--idle-ms") == 0) bit = 1U << 6;
		else if (strcmp(name, "--ready-file") == 0) bit = 1U << 7;
		else if (strcmp(name, "--drop") == 0) bit = 1U << 8;
		else if (strcmp(name, "--drop-fu") == 0) bit = 1U << 9;
		else if (strcmp(name, "--duplicate") == 0) bit = 1U << 10;
		else if (strcmp(name, "--reorder") == 0) bit = 1U << 11;
		else if (strcmp(name, "--blackout") == 0) bit = 1U << 12;
		else if (strcmp(name, "--blackout-ms") == 0) bit = 1U << 13;
		else if (strcmp(name, "--drop-vcl-fu") == 0) bit = 1U << 14;
		else return -1;
		if ((seen & bit) != 0)
			return -1;
		seen |= bit;
		if (bit == (1U << 2)) o->rx_path = arg;
		else if (bit == (1U << 3)) o->tx_path = arg;
		else if (bit == (1U << 4)) o->stats_path = arg;
		else if (bit == (1U << 7)) o->ready_path = arg;
		else if (bit == (1U << 8)) {
			if (drop_list(o, arg) != 0) return -1;
		} else if (bit == (1U << 12)) {
			if (range(arg, MAX_PACKETS, &o->blackout_first,
			    &o->blackout_last) != 0 || o->blackout_first == 0)
				return -1;
		} else if (bit == (1U << 13)) {
			if (range(arg, 3600000, &o->blackout_ms_first,
			    &o->blackout_ms_last) != 0)
				return -1;
			o->blackout_ms_enabled = 1;
		} else {
			if (number(arg, 1, bit <= 2 ? 65535U :
			    ((bit == (1U << 5) || bit == (1U << 6)) ?
			    3600000U : MAX_PACKETS), &value) != 0)
				return -1;
			if (bit == 1) o->listen_port = value;
			else if (bit == 2) o->forward_port = value;
			else if (bit == (1U << 5)) o->max_ms = value;
			else if (bit == (1U << 6)) o->idle_ms = value;
			else if (bit == (1U << 9)) o->drop_fu = value;
			else if (bit == (1U << 10)) o->duplicate = value;
			else if (bit == (1U << 11)) o->reorder = value;
			else if (bit == (1U << 14)) o->drop_vcl_fu = value;
		}
	}
	if ((seen & 127U) != 127U || o->listen_port == o->forward_port ||
	    o->idle_ms > o->max_ms ||
	    strcmp(o->rx_path, o->tx_path) == 0 ||
	    strcmp(o->rx_path, o->stats_path) == 0 ||
	    strcmp(o->tx_path, o->stats_path) == 0)
		return -1;
	if (o->ready_path != NULL &&
	    (strcmp(o->ready_path, o->rx_path) == 0 ||
	    strcmp(o->ready_path, o->tx_path) == 0 ||
	    strcmp(o->ready_path, o->stats_path) == 0))
		return -1;
	return 0;
}

static int
now_us(uint64_t *result)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0 || ts.tv_sec < 0)
		return -1;
	*result = (uint64_t)ts.tv_sec * UINT64_C(1000000) +
	    (uint64_t)ts.tv_nsec / UINT64_C(1000);
	return 0;
}

static void
put32(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char)(value >> 24);
	p[1] = (unsigned char)(value >> 16);
	p[2] = (unsigned char)(value >> 8);
	p[3] = (unsigned char)value;
}

static int
capture(FILE *fp, const unsigned char *packet, size_t len, uint32_t ordinal,
    uint32_t flags, uint64_t elapsed)
{
	unsigned char header[24];

	if ((uint64_t)len + sizeof(header) > MAX_CAPTURE_BYTES - capture_bytes) {
		errno = EFBIG;
		return -1;
	}
	put32(header, (uint32_t)len);
	put32(header + 4, ordinal);
	put32(header + 8, flags);
	put32(header + 12, 0);
	put32(header + 16, (uint32_t)(elapsed >> 32));
	put32(header + 20, (uint32_t)elapsed);
	if (fwrite(header, 1, sizeof(header), fp) != sizeof(header) ||
	    fwrite(packet, 1, len, fp) != len)
		return -1;
	capture_bytes += (uint64_t)len + sizeof(header);
	return 0;
}

/* Validate RTP framing and the three RFC 7798 forms without DONL. */
static int
packet_kind(const unsigned char *p, size_t len, int *middle)
{
	size_t offset, payload_len, ext, n;
	unsigned int type, marker;

	*middle = 0;
	if (len < 12 || p[0] >> 6 != 2)
		return -1;
	marker = p[1] & 128U;
	offset = 12U + 4U * (p[0] & 15U);
	if (offset > len)
		return -1;
	if ((p[0] & 16U) != 0) {
		if (len - offset < 4)
			return -1;
		ext = 4U + 4U * ((size_t)p[offset + 2] * 256U + p[offset + 3]);
		if (ext > len - offset)
			return -1;
		offset += ext;
	}
	payload_len = len - offset;
	if ((p[0] & 32U) != 0) {
		n = p[len - 1];
		if (n == 0 || n > payload_len)
			return -1;
		payload_len -= n;
	}
	if (payload_len < 2 || (p[offset] & 128U) != 0 ||
	    (p[offset + 1] & 7U) == 0)
		return -1;
	p += offset;
	type = (p[0] >> 1) & 63U;
	if (type <= 47)
		return 0;
	if (type == 49) {
		if (payload_len < 4 || (p[2] & 192U) == 192U ||
		    (p[2] & 63U) > 47 ||
		    ((p[2] & 64U) == 0 && marker != 0))
			return -1;
		/* Nonzero means a middle FU. Subtract one for its original type. */
		*middle = (p[2] & 192U) == 0 ? (int)(p[2] & 63U) + 1 : 0;
		return 0;
	}
	if (type != 48)
		return -1;
	offset = 2;
	while (offset < payload_len) {
		if (payload_len - offset < 2)
			return -1;
		n = (size_t)p[offset] * 256U + p[offset + 1];
		offset += 2;
		if (n < 2 || n > payload_len - offset ||
		    (p[offset] & 128U) != 0 || (p[offset + 1] & 7U) == 0 ||
		    ((p[offset] >> 1) & 63U) > 47)
			return -1;
		offset += n;
	}
	return offset == 2 ? -1 : 0;
}

static int
send_packet(int fd, const struct sockaddr_in *destination, FILE *tx,
    const unsigned char *packet, size_t len,
    uint32_t ordinal, uint32_t flags, uint64_t start, struct counters *c)
{
	ssize_t sent;
	uint64_t now;

	do {
		sent = sendto(fd, packet, len, 0,
		    (const struct sockaddr *)destination, sizeof(*destination));
	} while (sent < 0 && errno == EINTR);
	if (sent < 0 || (size_t)sent != len) {
		failure = "send";
		return -1;
	}
	c->forwarded++;
	if (now_us(&now) != 0 ||
	    capture(tx, packet, len, ordinal, F_TX | flags, now - start) != 0) {
		failure = "forward-capture";
		return -1;
	}
	return 0;
}

static int
stats(FILE *fp, const struct counters *c, const char *reason, uint64_t elapsed)
{
	return fprintf(fp,
	    "{\"format\":\"H265RTP1\",\"reason\":\"%s\",\"error\":%s,"
	    "\"receive_buffer_bytes\":%d,\"received\":%" PRIu32 ",\"forwarded\":%" PRIu32 ","
	    "\"dropped\":%" PRIu32 ",\"malformed\":%" PRIu32 ","
	    "\"truncated\":%" PRIu32 ",\"middle_fu\":%" PRIu32 ","
	    "\"middle_vcl_fu\":%" PRIu32 ",\"selected_fu_drops\":%" PRIu32 ","
	    "\"selected_fu_type\":%d,"
	    "\"duplicate_sends\":%" PRIu32 ",\"reorder_holds\":%" PRIu32 ","
	    "\"blackout_drops\":%" PRIu32 ",\"held_at_end\":%" PRIu32 ","
	    "\"elapsed_us\":%" PRIu64 "}\n", reason,
	    failure == NULL ? "null" : "true", c->receive_buffer_bytes,
	    c->received, c->forwarded,
	    c->dropped, c->malformed, c->truncated, c->middle_fu,
	    c->middle_vcl_fu, c->selected_fu_drops, c->selected_fu_type,
	    c->duplicate_sends, c->reorder_holds, c->blackout_drops,
	    c->held_at_end, elapsed) < 0 ? -1 : 0;
}

int
main(int argc, char **argv)
{
	struct options o;
	struct counters c = {0};
	struct sockaddr_in addr;
	struct pollfd pollfd;
	struct msghdr msg;
	struct iovec iov;
	unsigned char packet[MAX_PACKET], held[MAX_PACKET];
	FILE *rx = NULL, *tx = NULL, *report = NULL, *ready;
	uint64_t start = 0, now = 0, last = 0, first = 0, wait_us, deadline;
	uint32_t flags, held_ordinal = 0, i;
	size_t held_len = 0;
	ssize_t len;
	const char *reason = "initialization";
	int fd = -1, out = -1, status = EXIT_FAILURE, result, middle, drop;
	int receive_buffer = 1048576;
	socklen_t buffer_length = sizeof(c.receive_buffer_bytes);

	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		usage();
		return EXIT_SUCCESS;
	}
	c.selected_fu_type = -1;
	if (options(argc, argv, &o) != 0) {
		usage();
		return 64;
	}
	if (now_us(&start) != 0) { failure = "clock"; goto done; }
	now = last = start;
	rx = fopen(o.rx_path, "wb");
	tx = fopen(o.tx_path, "wb");
	report = fopen(o.stats_path, "wb");
	if (rx == NULL || tx == NULL || report == NULL) {
		failure = "open-output"; goto done;
	}
	if (fwrite("H265RTP1", 1, 8, rx) != 8 ||
	    fwrite("H265RTP1", 1, 8, tx) != 8) {
		failure = "capture-header"; goto done;
	}
	capture_bytes = 16;
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	out = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0 || out < 0) { failure = "socket"; goto done; }
	/* Keep producer bursts separate from the deliberate packet faults. */
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
	    sizeof(receive_buffer)) != 0) {
		receive_buffer = 262144;
		if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
		    sizeof(receive_buffer)) != 0) {
			failure = "receive-buffer-set"; goto done;
		}
	}
	if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &c.receive_buffer_bytes,
	    &buffer_length) != 0 || buffer_length != sizeof(c.receive_buffer_bytes) ||
	    c.receive_buffer_bytes < 262144) {
		failure = "receive-buffer-size"; goto done;
	}
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1 ||
	    fcntl(out, F_SETFL, O_NONBLOCK) == -1) {
		failure = "nonblocking"; goto done;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((uint16_t)o.listen_port);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		failure = "bind"; goto done;
	}
	addr.sin_port = htons((uint16_t)o.forward_port);
	if (o.ready_path != NULL) {
		ready = fopen(o.ready_path, "wb");
		if (ready == NULL) { failure = "ready-open"; goto done; }
		result = fprintf(ready, "%ld\n", (long)getpid());
		if (fclose(ready) != 0 || result < 0) {
			failure = "ready-write"; goto done;
		}
	}
	pollfd.fd = fd;
	pollfd.events = POLLIN;
	for (;;) {
		if (now_us(&now) != 0) { failure = "clock"; break; }
		if (now - start >= (uint64_t)o.max_ms * 1000U) {
			reason = c.received == 0 ? "no-input-deadline" : "deadline";
			status = c.received == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
			break;
		}
		if (now - last >= (uint64_t)o.idle_ms * 1000U) {
			reason = c.received == 0 ? "no-input" : "idle";
			status = c.received == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
			break;
		}
		deadline = start + (uint64_t)o.max_ms * 1000U;
		if (last + (uint64_t)o.idle_ms * 1000U < deadline)
			deadline = last + (uint64_t)o.idle_ms * 1000U;
		wait_us = deadline - now;
		result = poll(&pollfd, 1, (int)((wait_us + 999U) / 1000U));
		if (result < 0 && errno == EINTR)
			continue;
		if (result < 0) { failure = "poll"; break; }
		if (result == 0)
			continue;
		if ((pollfd.revents & POLLIN) == 0) { failure = "poll-events"; break; }
		memset(&msg, 0, sizeof(msg));
		iov.iov_base = packet;
		iov.iov_len = sizeof(packet);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		len = recvmsg(fd, &msg, 0);
		if (len < 0 && (errno == EINTR || errno == EAGAIN
#if EWOULDBLOCK != EAGAIN
		    || errno == EWOULDBLOCK
#endif
		    ))
			continue;
		if (len < 0) { failure = "receive"; break; }
		if (now_us(&now) != 0) { failure = "clock"; break; }
		last = now;
		if (c.received == 0)
			first = now;
		c.received++;
		if ((msg.msg_flags & MSG_TRUNC) != 0 || (size_t)len > sizeof(packet)) {
			c.truncated++; failure = "truncated-datagram"; break;
		}
		flags = F_RX;
		middle = 0;
		drop = packet_kind(packet, (size_t)len, &middle) != 0;
		if (drop) { c.malformed++; flags |= F_INVALID; }
		if (middle)
			c.middle_fu++;
		if (middle > 0 && middle <= 32)
			c.middle_vcl_fu++;
		if ((o.drop_fu != 0 && middle && c.middle_fu == o.drop_fu) ||
		    (o.drop_vcl_fu != 0 && middle > 0 && middle <= 32 &&
		    c.middle_vcl_fu == o.drop_vcl_fu)) {
			drop = 1;
			c.selected_fu_drops++;
			c.selected_fu_type = middle - 1;
		}
		for (i = 0; i < o.drop_count; i++)
			if (c.received == o.drops[i]) drop = 1;
		if ((o.blackout_first != 0 && c.received >= o.blackout_first &&
		    c.received <= o.blackout_last) ||
		    (o.blackout_ms_enabled &&
		    (now - first) / 1000U >= o.blackout_ms_first &&
		    (now - first) / 1000U <= o.blackout_ms_last)) {
			drop = 1; flags |= F_BLACKOUT; c.blackout_drops++;
		}
		if (drop) { flags |= F_DROP; c.dropped++; }
		else if (c.received == o.reorder) flags |= F_HELD;
		if (capture(rx, packet, (size_t)len, c.received, flags,
		    now - start) != 0) { failure = "receive-capture"; break; }
		if (!drop && c.received == o.reorder) {
			memcpy(held, packet, (size_t)len);
			held_len = (size_t)len; held_ordinal = c.received;
			c.reorder_holds++;
		} else if (!drop) {
			if (send_packet(out, &addr, tx, packet, (size_t)len, c.received,
			    0, start, &c) != 0) break;
			if (c.received == o.duplicate) {
				if (send_packet(out, &addr, tx, packet, (size_t)len, c.received,
				    F_DUP, start, &c) != 0) break;
				c.duplicate_sends++;
			}
			if (held_ordinal != 0) {
				if (send_packet(out, &addr, tx, held, held_len, held_ordinal,
				    F_REORDER, start, &c) != 0) break;
				if (held_ordinal == o.duplicate) {
					if (send_packet(out, &addr, tx, held, held_len, held_ordinal,
					    F_REORDER | F_DUP, start, &c) != 0) break;
					c.duplicate_sends++;
				}
				held_ordinal = 0;
			}
		}
		if (c.received >= MAX_PACKETS) {
			reason = "packet-limit"; status = EXIT_SUCCESS; break;
		}
	}
done:
	if (held_ordinal != 0) c.held_at_end++;
	if (rx != NULL && fclose(rx) != 0 && failure == NULL)
		failure = "receive-capture-close";
	if (tx != NULL && fclose(tx) != 0 && failure == NULL)
		failure = "forward-capture-close";
	if (fd >= 0 && close(fd) != 0 && failure == NULL)
		failure = "receive-socket-close";
	if (out >= 0 && close(out) != 0 && failure == NULL)
		failure = "forward-socket-close";
	if (failure != NULL) {
		reason = failure;
		fprintf(stderr, "rtp-fault-relay: %s\n", failure);
		status = EXIT_FAILURE;
	}
	if (report != NULL) {
		if (stats(report, &c, reason, now >= start ? now - start : 0) != 0) {
			fputs("rtp-fault-relay: stats-write\n", stderr);
			status = EXIT_FAILURE;
		}
		if (fclose(report) != 0) {
			fputs("rtp-fault-relay: stats-close\n", stderr);
			status = EXIT_FAILURE;
		}
	}
	return status;
}
