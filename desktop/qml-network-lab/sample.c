/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sample.h"

#define NTP_PACKET_SIZE 48
#define NTP_PORT_TIMEOUT_MS 1500
#define NTP_UNIX_EPOCH_DELTA UINT64_C(2208988800)
#define NTP_ERA_SECONDS UINT64_C(4294967296)
#define NTP_SANITY_START INT64_C(1577836800) /* 2020-01-01 UTC */
#define NTP_SANITY_END INT64_C(4102444800)   /* 2100-01-01 UTC */

static int
monotonic_ns(uint64_t *value)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1 || ts.tv_sec < 0 ||
	    ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
		return -1;
	*value = (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
	    (uint64_t)ts.tv_nsec;
	return 0;
}

static uint32_t
elapsed_ms(uint64_t start, uint64_t end)
{
	uint64_t ms;

	if (end < start)
		return 0;
	ms = (end - start) / UINT64_C(1000000);
	return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static int
first_ipv4(const char *host, const char *service, struct sockaddr_in *out)
{
	struct addrinfo hints, *list;
	const struct addrinfo *ai;
	int error;

	if (host == NULL || host[0] == '\0' || service == NULL ||
	    service[0] == '\0')
		return -1;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	error = getaddrinfo(host, service, &hints, &list);
	if (error != 0)
		return -1;
	for (ai = list; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET && ai->ai_addrlen >= sizeof(*out)) {
			memcpy(out, ai->ai_addr, sizeof(*out));
			freeaddrinfo(list);
			return 0;
		}
	}
	freeaddrinfo(list);
	return -1;
}

void
sample_dns(const char *host, struct net_sample *sample)
{
	struct sockaddr_in address;
	uint64_t start, end;
	char text[INET_ADDRSTRLEN];
	int result;

	if (sample == NULL)
		return;
	memset(sample, 0, sizeof(*sample));
	if (monotonic_ns(&start) == -1)
		return;
	result = first_ipv4(host, "0", &address);
	if (monotonic_ns(&end) == -1)
		return;
	sample->dns_ms = elapsed_ms(start, end);
	if (result == -1)
		return;
	if (inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text)) == NULL)
		return;
	memcpy(sample->address, text, strlen(text) + 1);
	sample->dns_ok = true;
}

static void
put_u32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static uint32_t
get_u32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int
request_timestamp(uint8_t packet[NTP_PACKET_SIZE])
{
	struct timespec ts;
	uint64_t ntp_seconds, fraction;
	uint32_t low_seconds;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1 || ts.tv_sec < 0 ||
	    ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
		return -1;
	ntp_seconds = (uint64_t)ts.tv_sec + NTP_UNIX_EPOCH_DELTA;
	low_seconds = (uint32_t)ntp_seconds;
	fraction = ((uint64_t)ts.tv_nsec << 32) / UINT64_C(1000000000);
	put_u32(packet + 40, low_seconds);
	put_u32(packet + 44, (uint32_t)fraction);
	/* The all-zero timestamp is reserved to mean an unsynchronized clock. */
	if (low_seconds == 0 && fraction == 0)
		packet[47] = 1;
	return 0;
}

static int
decode_utc(const uint8_t *packet, int64_t *utc, uint32_t *fraction_ms)
{
	uint32_t seconds = get_u32(packet + 40);
	uint32_t fraction = get_u32(packet + 44);
	int64_t candidate;
	unsigned int era;

	if (seconds == 0 && fraction == 0)
		return -1;
	/*
	 * The board clock is deliberately not the era reference.  Unfold the
	 * 32-bit seconds field over eras 0..2, then accept only 2020..2099 UTC.
	 * That window covers current deployments and avoids mistaking an old,
	 * unsynchronized RTC for a valid NTP date.
	 */
	for (era = 0; era <= 2; era++) {
		uint64_t expanded = (uint64_t)era * NTP_ERA_SECONDS + seconds;

		if (expanded < NTP_UNIX_EPOCH_DELTA)
			continue;
		candidate = (int64_t)(expanded - NTP_UNIX_EPOCH_DELTA);
		if (candidate >= NTP_SANITY_START && candidate < NTP_SANITY_END) {
			*utc = candidate;
			*fraction_ms = (uint32_t)(((uint64_t)fraction * 1000) >> 32);
			return 0;
		}
	}
	return -1;
}

static int
wait_readable(int fd, uint64_t start_ns)
{
	struct pollfd pfd;
	uint64_t now_ns, deadline_ns;

	deadline_ns = start_ns + (uint64_t)NTP_PORT_TIMEOUT_MS * 1000000;
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	for (;;) {
		uint64_t remaining_ns;
		int timeout, result;

		if (monotonic_ns(&now_ns) == -1)
			return -1;
		if (now_ns >= deadline_ns) {
			errno = ETIMEDOUT;
			return -1;
		}
		remaining_ns = deadline_ns - now_ns;
		timeout = (int)((remaining_ns + 999999) / 1000000);
		result = poll(&pfd, 1, timeout);
		if (result > 0)
			return (pfd.revents & POLLIN) != 0 ? 0 : -1;
		if (result == 0) {
			errno = ETIMEDOUT;
			return -1;
		}
		if (errno != EINTR)
			return -1;
	}
}

void
sample_clock(const char *host, const char *service, struct net_sample *sample)
{
	struct sockaddr_in server;
	uint8_t request[NTP_PACKET_SIZE], response[512];
	struct iovec iov;
	struct msghdr msg;
	uint64_t start_ns, end_ns;
	int fd = -1;
	ssize_t n;
	unsigned int version, mode, leap, stratum;
	int64_t utc;
	uint32_t fraction_ms;

	if (sample == NULL)
		return;
	memset(sample, 0, sizeof(*sample));
	if (first_ipv4(host, service, &server) == -1)
		return;
	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd == -1)
		goto out;
	if (connect(fd, (struct sockaddr *)&server, sizeof(server)) == -1)
		goto out;
	memset(request, 0, sizeof(request));
	request[0] = (uint8_t)((4U << 3) | 3U); /* LI=0, VN=4, client mode */
	if (request_timestamp(request) == -1)
		goto out;
	if (monotonic_ns(&start_ns) == -1)
		goto out;
	if (send(fd, request, sizeof(request), 0) != (ssize_t)sizeof(request))
		goto out;
	if (wait_readable(fd, start_ns) == -1)
		goto out;
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = response;
	iov.iov_len = sizeof(response);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	n = recvmsg(fd, &msg, 0);
	if (n < NTP_PACKET_SIZE || (msg.msg_flags & MSG_TRUNC) != 0)
		goto out;
	if (monotonic_ns(&end_ns) == -1)
		goto out;
	leap = response[0] >> 6;
	version = (response[0] >> 3) & 7U;
	mode = response[0] & 7U;
	stratum = response[1];
	if ((version != 3 && version != 4) || mode != 4 || leap == 3 ||
	    stratum == 0 || stratum > 15 ||
	    memcmp(response + 24, request + 40, 8) != 0 ||
	    decode_utc(response, &utc, &fraction_ms) == -1)
		goto out;
	sample->ntp_ms = elapsed_ms(start_ns, end_ns);
	sample->utc_seconds = utc;
	sample->fraction_ms = fraction_ms;
	sample->received_ms = end_ns / UINT64_C(1000000);
	sample->clock_ok = true;
out:
	if (fd != -1)
		(void)close(fd);
}
