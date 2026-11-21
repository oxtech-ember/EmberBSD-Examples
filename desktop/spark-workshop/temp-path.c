/* SPDX-License-Identifier: BSD-2-Clause */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "temp-path.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

int
workshop_temp_template(const char *directory, const char *prefix, char **out)
{
	size_t directory_length, prefix_length, separator, length;
	char *path;
	int n;

	if (directory == NULL || prefix == NULL || out == NULL ||
	    directory[0] != '/' || directory[0] == '\0' || prefix[0] == '\0' ||
	    strchr(prefix, '/') != NULL) {
		errno = EINVAL;
		return -1;
	}
	*out = NULL;
	directory_length = strlen(directory);
	prefix_length = strlen(prefix);
	separator = directory[directory_length - 1] == '/' ? 0 : 1;
	if (prefix_length > SIZE_MAX - separator - 8 ||
	    directory_length > SIZE_MAX - prefix_length - separator - 8) {
		errno = ENAMETOOLONG;
		return -1;
	}
	length = directory_length + separator + prefix_length + 7 + 1;
	if (length > PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}
	path = malloc(length);
	if (path == NULL)
		return -1;
	n = snprintf(path, length, "%s%s%s.XXXXXX", directory,
	    separator == 0 ? "" : "/", prefix);
	if (n < 0 || (size_t)n + 1 != length) {
		free(path);
		errno = EINVAL;
		return -1;
	}
	*out = path;
	return 0;
}
