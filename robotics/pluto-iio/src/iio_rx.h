/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef IIO_RX_H
#define IIO_RX_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#define RX_COMMAND_MAX 512
#define RX_PAYLOAD_MAX 1024
#define RX_CHUNK_MAX 16384
struct rx_ops {
	int (*attr)(void *, const char *, const char *, const char *, char *, size_t, int);
	int (*start)(void *, unsigned);
	ssize_t (*read)(void *, void *, size_t);
	int (*stop)(void *);
	int (*send)(void *, unsigned, const void *, size_t);
	int (*settings)(void *, unsigned, unsigned);
};
struct rx_pipe {
	char line[RX_COMMAND_MAX], payload[RX_PAYLOAD_MAX + 1];
	size_t used, pending, received;
	int poisoned;
};
struct rx_service {
	struct rx_ops ops;
	void *arg;
	struct rx_pipe pipe[2];
	int active, owner;
};
void rx_init(struct rx_service *, const struct rx_ops *, void *);
int rx_feed(struct rx_service *, unsigned, const void *, size_t);
int rx_disconnect(struct rx_service *);
const char *rx_xml(void);
#endif
