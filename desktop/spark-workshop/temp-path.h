/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SPARK_WORKSHOP_TEMP_PATH_H
#define SPARK_WORKSHOP_TEMP_PATH_H

/* Build an absolute, bounded mkstemp template under dir. Caller frees *out. */
int workshop_temp_template(const char *, const char *, char **);

#endif
