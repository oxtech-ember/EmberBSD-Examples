/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../temp-path.h"

int
main(void)
{
	char *path;
	char too_long[PATH_MAX + 1];

	assert(workshop_temp_template("/tmp", "spark-photo", &path) == 0);
	assert(strcmp(path, "/tmp/spark-photo.XXXXXX") == 0);
	free(path);
	path = NULL;
	assert(workshop_temp_template("/tmp/cache/", "spark-ask", &path) == 0);
	assert(strcmp(path, "/tmp/cache/spark-ask.XXXXXX") == 0);
	free(path);
	errno = 0;
	assert(workshop_temp_template("tmp", "spark-photo", &path) == -1);
	assert(errno == EINVAL);
	memset(too_long, 'x', sizeof(too_long));
	too_long[0] = '/';
	too_long[sizeof(too_long) - 1] = '\0';
	errno = 0;
	assert(workshop_temp_template(too_long, "spark-photo", &path) == -1);
	assert(errno == ENAMETOOLONG);
	puts("temporary path: PASS");
	return 0;
}
