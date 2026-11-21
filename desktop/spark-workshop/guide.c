/* SPDX-License-Identifier: BSD-2-Clause */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "guide.h"

enum { GUIDE_MAX_BYTES = 16384, MAX_LINE_CHARS = WORKSHOP_LINE_BYTES - 1,
	BODY_WRAP_CHARS = 42 };

static int
invalid(void)
{
	errno = EINVAL;
	return -1;
}

static int
copy_text(char *out, size_t capacity, const char *text, size_t length)
{
	while (length != 0 && (*text == ' ' || *text == '\t')) {
		text++;
		length--;
	}
	while (length != 0 && (text[length - 1] == ' ' || text[length - 1] == '\t'))
		length--;
	if (length == 0 || length >= capacity)
		return invalid();
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)text[i];
		if (c < 0x20 || c > 0x7e)
			return invalid();
	}
	memcpy(out, text, length);
	out[length] = '\0';
	return 0;
}

static int
wrap_paragraph(struct workshop_step *step)
{
	const char *p = step->paragraph;
	size_t used = 0;
	unsigned int line = 0;

	while (*p != '\0') {
		const char *word;
		size_t length;

		while (*p == ' ')
			p++;
		if (*p == '\0')
			break;
		word = p;
		while (*p != '\0' && *p != ' ')
			p++;
		length = (size_t)(p - word);
		if (length > BODY_WRAP_CHARS)
			return invalid();
		if (line == WORKSHOP_TEXT_LINES)
			return invalid();
		if (used != 0 && used + 1 + length > BODY_WRAP_CHARS) {
			line++;
			used = 0;
			if (line == WORKSHOP_TEXT_LINES)
				return invalid();
		}
		if (used != 0)
			step->lines[line][used++] = ' ';
		memcpy(step->lines[line] + used, word, length);
		used += length;
		step->lines[line][used] = '\0';
	}
	step->line_count = line + 1;
	return 0;
}

static int
blank(const char *line, size_t length)
{
	for (size_t i = 0; i < length; i++)
		if (line[i] != ' ' && line[i] != '\t')
			return 0;
	return 1;
}

int
workshop_guide_load(const char *path, struct workshop_guide *guide)
{
	char bytes[GUIDE_MAX_BYTES + 1];
	struct stat st;
	size_t used = 0, offset = 0;
	int fd, state = 0, error = 0;

	if (path == NULL || path[0] != '/' || guide == NULL)
		return invalid();
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return -1;
	if (fstat(fd, &st) == -1) {
		error = errno;
		goto out;
	}
	if (!S_ISREG(st.st_mode)) {
		error = EINVAL;
		goto out;
	}
	if (st.st_size < 0 || st.st_size > GUIDE_MAX_BYTES) {
		error = EFBIG;
		goto out;
	}
	while (used < sizeof(bytes)) {
		ssize_t count = read(fd, bytes + used, sizeof(bytes) - used);
		if (count == -1 && errno == EINTR)
			continue;
		if (count == -1) {
			error = errno;
			goto out;
		}
		if (count == 0)
			break;
		used += (size_t)count;
	}
	if (used > GUIDE_MAX_BYTES || used == sizeof(bytes)) {
		error = EFBIG;
		goto out;
	}
	if (close(fd) == -1)
		return -1;
	fd = -1;
	bytes[used] = '\0';
	memset(guide, 0, sizeof(*guide));
	while (offset < used) {
		size_t end = offset, length;
		const char *line = bytes + offset;
		struct workshop_step *step;

		while (end < used && bytes[end] != '\n')
			end++;
		length = end - offset;
		if (length != 0 && line[length - 1] == '\r')
			length--;
		for (size_t i = 0; i < length; i++) {
			unsigned char c = (unsigned char)line[i];
			if (c == '\0' || c == '\r' || (c < 0x20 && c != '\t') || c > 0x7e)
				return invalid();
		}
		if (blank(line, length)) {
			offset = end + (end < used);
			continue;
		}
		if (state == 0) {
			if (length < 3 || line[0] != '#' || line[1] != ' ' ||
			    copy_text(guide->title, sizeof(guide->title), line + 2, length - 2) == -1 ||
			    strlen(guide->title) > 26)
				return invalid();
			state = 1;
		} else if (state == 1 || state == 3) {
			if (length < 4 || line[0] != '#' || line[1] != '#' || line[2] != ' ' ||
			    guide->step_count == WORKSHOP_MAX_STEPS)
				return invalid();
			step = &guide->steps[guide->step_count];
			if (copy_text(step->title, sizeof(step->title), line + 3, length - 3) == -1 ||
			    strlen(step->title) > 29)
				return -1;
			guide->step_count++;
			state = 2;
		} else {
			if (line[0] == '#' || guide->step_count == 0)
				return invalid();
			step = &guide->steps[guide->step_count - 1];
			if (copy_text(step->paragraph, sizeof(step->paragraph), line, length) == -1 ||
			    wrap_paragraph(step) == -1)
				return -1;
			state = 3;
		}
		offset = end + (end < used);
	}
	if (state != 3 || guide->step_count == 0)
		return invalid();
	return 0;
out:
	if (close(fd) == -1 && error == 0)
		error = errno;
	errno = error;
	return -1;
}
