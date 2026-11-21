/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_VIEW_H
#define SPARK_WORKSHOP_VIEW_H

#include "eqml.h"
#include "eqml_input.h"
#include "guide.h"

int workshop_view_home(struct eqml_ui *, const struct workshop_guide *);
int workshop_view_step(struct eqml_ui *, const struct workshop_guide *,
	unsigned int, const struct eqml_bitmap *);
int workshop_view_camera(struct eqml_ui *, unsigned int, const char *,
	const struct eqml_bitmap *, int);

#endif
