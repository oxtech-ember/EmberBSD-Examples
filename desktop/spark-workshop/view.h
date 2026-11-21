/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_VIEW_H
#define SPARK_WORKSHOP_VIEW_H

#include "eqml.h"
#include "eqml_input.h"
#include "answer.h"
#include "guide.h"

enum workshop_answer_state { WORKSHOP_ANSWER_WAITING, WORKSHOP_ANSWER_READY,
	WORKSHOP_ANSWER_ERROR };

int workshop_view_home(struct eqml_ui *, const struct workshop_guide *);
int workshop_view_step(struct eqml_ui *, const struct workshop_guide *,
	unsigned int, const struct eqml_bitmap *);
int workshop_view_camera(struct eqml_ui *, unsigned int, const char *,
	const struct eqml_bitmap *, int);
int workshop_view_answer(struct eqml_ui *, const struct eqml_bitmap *,
	enum workshop_answer_state, size_t, size_t,
	const char [WORKSHOP_ANSWER_LINES][WORKSHOP_ANSWER_LINE_BYTES]);

#endif
