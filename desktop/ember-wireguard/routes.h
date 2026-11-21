/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_WG_ROUTES_H
#define EMBER_WG_ROUTES_H

#include <stddef.h>

/* Fixed argv, no shell. Capture stdout AND stderr, NUL-terminated, when
 * output is non-NULL. Report truncation/read/wait errors as failure. */
typedef int (*wg_route_runner)(void *, const char *const *, char *, size_t);

int wg_routes_up(const char *, const char *, const char *, const char *,
    const char [][128], size_t, const char *, wg_route_runner, void *);
int wg_routes_down(const char *, const char *, wg_route_runner, void *);

#endif
