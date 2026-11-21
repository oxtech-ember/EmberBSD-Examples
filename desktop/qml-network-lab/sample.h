/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef QML_NETWORK_LAB_SAMPLE_H
#define QML_NETWORK_LAB_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>

struct net_sample {
	bool dns_ok;
	bool clock_ok;
	uint32_t dns_ms;
	uint32_t ntp_ms;
	int64_t utc_seconds;
	uint32_t fraction_ms;
	uint64_t received_ms;
	char address[INET_ADDRSTRLEN];
};

/*
 * Each call clears the entire output and performs only its named probe.  The
 * caller may merge the DNS and clock fields after running separate workers.
 * DNS measures one IPv4 getaddrinfo lookup; dns_ms preserves its duration even
 * on lookup failure, while address stays empty.  The clock probe performs one
 * read-only SNTPv4 exchange; ntp_ms, utc_seconds, fraction_ms, and received_ms
 * are set only for a validated reply.  received_ms is the monotonic receive
 * time in milliseconds since an unspecified boot-relative origin.  The caller
 * should run each blocking function in a worker with its own deadline.  The
 * SNTP receive wait itself is bounded to 1500 ms; getaddrinfo is subject to
 * the caller's process deadline.  No system clock is changed.
 */
void sample_dns(const char *, struct net_sample *);
void sample_clock(const char *, const char *, struct net_sample *);

#endif /* QML_NETWORK_LAB_SAMPLE_H */
