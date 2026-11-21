/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_ANSWER_H
#define SPARK_WORKSHOP_ANSWER_H

#include <stddef.h>

enum {
	WORKSHOP_ANSWER_MAX_BYTES = 2048,
	WORKSHOP_ANSWER_LINES = 7,
	WORKSHOP_ANSWER_COLUMNS = 24,
	WORKSHOP_ANSWER_LINE_BYTES = WORKSHOP_ANSWER_COLUMNS + 1,
	WORKSHOP_ANSWER_MAX_PAGES = 13
};

struct workshop_answer {
	size_t page_count;
	char pages[WORKSHOP_ANSWER_MAX_PAGES][WORKSHOP_ANSWER_LINES]
	    [WORKSHOP_ANSWER_LINE_BYTES];
};

/* Parse bounded printable ASCII worker output into pages for the QML font. */
int workshop_answer_parse(const char *, size_t, struct workshop_answer *);
/* Parse one LF or CRLF terminated worker response line. */
int workshop_answer_parse_response(const char *, size_t,
	struct workshop_answer *);

#endif
