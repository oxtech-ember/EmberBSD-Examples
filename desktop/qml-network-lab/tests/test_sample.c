/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../sample.h"

#define NTP_SECONDS_2026 UINT32_C(4004208000)
#define NTP_SECONDS_2040 UINT32_C(123010304)
#define NTP_SECONDS_2010 UINT32_C(3471292800)

enum reply_kind {
	REPLY_VALID,
	REPLY_SHORT,
	REPLY_BAD_VERSION,
	REPLY_BAD_MODE,
	REPLY_BAD_LI,
	REPLY_BAD_STRATUM,
	REPLY_BAD_ORIGINATE,
	REPLY_ERA_ONE,
	REPLY_OLD_TIME,
	REPLY_DROP
};

struct server {
	int fd;
	uint16_t port;
	enum reply_kind kind;
	uint32_t ntp_seconds;
};

static int start_server(struct server *, enum reply_kind, pthread_t *,
	char [6]);

static void
must_start_server(struct server *server, enum reply_kind kind,
	pthread_t *thread, char service[6])
{
	if (start_server(server, kind, thread, service) == -1) {
		perror("start loopback SNTP server");
		exit(1);
	}
}

static void
put_u32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static void *
serve(void *arg)
{
	struct server *server = arg;
	struct sockaddr_in peer;
	socklen_t peerlen = sizeof(peer);
	uint8_t request[512], response[48];
	ssize_t n;

	n = recvfrom(server->fd, request, sizeof(request), 0,
	    (struct sockaddr *)&peer, &peerlen);
	if (n < 48)
		return NULL;
	if (server->kind == REPLY_DROP)
		return NULL;
	memset(response, 0, sizeof(response));
	response[0] = 0x24; /* LI=0, VN=4, server mode */
	response[1] = 2;
	memcpy(response + 24, request + 40, 8);
	put_u32(response + 40, server->ntp_seconds);
	put_u32(response + 44, UINT32_C(0x40000000));
	switch (server->kind) {
	case REPLY_SHORT:
		n = 12;
		break;
	case REPLY_BAD_VERSION:
		response[0] = 0x14; /* VN=2, mode=4 */
		break;
	case REPLY_BAD_MODE:
		response[0] = 0x23; /* client mode */
		break;
	case REPLY_BAD_LI:
		response[0] = 0xe4; /* LI=3 */
		break;
	case REPLY_BAD_STRATUM:
		response[1] = 0;
		break;
	case REPLY_BAD_ORIGINATE:
		response[24] ^= 1;
		break;
	case REPLY_ERA_ONE:
	case REPLY_OLD_TIME:
	case REPLY_VALID:
	case REPLY_DROP:
		break;
	}
	(void)sendto(server->fd, response, (size_t)n, 0,
	    (struct sockaddr *)&peer, peerlen);
	return NULL;
}

static int
start_server(struct server *server, enum reply_kind kind, pthread_t *thread,
    char service[6])
{
	struct sockaddr_in address;
	socklen_t length = sizeof(address);

	server->fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (server->fd == -1)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(server->fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
	    getsockname(server->fd, (struct sockaddr *)&address, &length) == -1) {
		(void)close(server->fd);
		return -1;
	}
	server->port = ntohs(address.sin_port);
	server->kind = kind;
	server->ntp_seconds = kind == REPLY_ERA_ONE ? NTP_SECONDS_2040 :
	    kind == REPLY_OLD_TIME ? NTP_SECONDS_2010 : NTP_SECONDS_2026;
	(void)snprintf(service, 6, "%u", (unsigned int)server->port);
	if (pthread_create(thread, NULL, serve, server) != 0) {
		(void)close(server->fd);
		return -1;
	}
	return 0;
}

static void
run_case(enum reply_kind kind, bool expect_clock, int64_t expected_utc)
{
	struct server server;
	struct net_sample dns, clock;
	pthread_t thread;
	char service[6];

	must_start_server(&server, kind, &thread, service);
	sample_dns("localhost", &dns);
	sample_clock("127.0.0.1", service, &clock);
	assert(pthread_join(thread, NULL) == 0);
	(void)close(server.fd);
	assert(dns.dns_ok);
	assert(strcmp(dns.address, "127.0.0.1") == 0);
	assert(clock.clock_ok == expect_clock);
	if (expect_clock) {
		assert(clock.utc_seconds == expected_utc);
		assert(clock.fraction_ms == 250);
		assert(clock.received_ms > 0);
		assert(clock.ntp_ms < 1500);
	} else {
		assert(clock.utc_seconds == 0);
		assert(clock.ntp_ms == 0);
		assert(clock.fraction_ms == 0);
		assert(clock.received_ms == 0);
	}
}

static void
run_dns_failure_case(void)
{
	struct server server;
	struct net_sample dns, clock;
	pthread_t thread;
	char service[6];

	must_start_server(&server, REPLY_VALID, &thread, service);
	sample_dns("bad host", &dns);
	sample_clock("127.0.0.1", service, &clock);
	assert(pthread_join(thread, NULL) == 0);
	(void)close(server.fd);
	assert(!dns.dns_ok);
	assert(dns.address[0] == '\0');
	assert(clock.clock_ok);
	assert(clock.utc_seconds == INT64_C(1795219200));
}

int
main(void)
{
	run_case(REPLY_VALID, true, INT64_C(1795219200));
	run_case(REPLY_SHORT, false, 0);
	run_case(REPLY_BAD_VERSION, false, 0);
	run_case(REPLY_BAD_MODE, false, 0);
	run_case(REPLY_BAD_LI, false, 0);
	run_case(REPLY_BAD_STRATUM, false, 0);
	run_case(REPLY_BAD_ORIGINATE, false, 0);
	run_case(REPLY_ERA_ONE, true, INT64_C(2208988800));
	run_case(REPLY_OLD_TIME, false, 0);
	run_case(REPLY_DROP, false, 0);
	run_dns_failure_case();
	(void)puts("network sample loopback tests passed");
	return 0;
}
