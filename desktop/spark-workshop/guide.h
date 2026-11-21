/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_GUIDE_H
#define SPARK_WORKSHOP_GUIDE_H

#define WORKSHOP_MAX_STEPS 8
#define WORKSHOP_TEXT_LINES 3
#define WORKSHOP_LINE_BYTES 48

struct workshop_step {
	char title[80];
	char paragraph[256];
	char lines[WORKSHOP_TEXT_LINES][WORKSHOP_LINE_BYTES];
	unsigned int line_count;
};

struct workshop_guide {
	char title[80];
	unsigned int step_count;
	struct workshop_step steps[WORKSHOP_MAX_STEPS];
};

/* Reads one bounded ASCII Markdown guide from an absolute local path. */
int workshop_guide_load(const char *, struct workshop_guide *);

#endif
