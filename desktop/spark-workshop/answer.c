/* SPDX-License-Identifier: BSD-2-Clause */

#include <errno.h>
#include <string.h>

#include "answer.h"

static int
next_line(struct workshop_answer *answer, size_t *page, size_t *line,
	size_t *column)
{
	*column = 0;
	if (++*line == WORKSHOP_ANSWER_LINES) {
		*line = 0;
		if (++*page == WORKSHOP_ANSWER_MAX_PAGES) {
			errno = EFBIG;
			return -1;
		}
		answer->page_count = *page + 1;
	}
	return 0;
}

int
workshop_answer_parse(const char *text, size_t length, struct workshop_answer *answer)
{
	char normalized[WORKSHOP_ANSWER_MAX_BYTES + 1];
	size_t used = 0, offset = 0, page = 0, line = 0, column = 0;

	if (text == NULL || answer == NULL || length == 0) {
		errno = EINVAL;
		return -1;
	}
	if (length > WORKSHOP_ANSWER_MAX_BYTES) {
		errno = EFBIG;
		return -1;
	}
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)text[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			if (used != 0 && normalized[used - 1] != ' ')
				normalized[used++] = ' ';
			continue;
		}
		if (c < 0x21 || c > 0x7e) {
			errno = EILSEQ;
			return -1;
		}
		normalized[used++] = (char)c;
	}
	while (used != 0 && normalized[used - 1] == ' ')
		used--;
	if (used == 0) {
		errno = EINVAL;
		return -1;
	}
	normalized[used] = '\0';
	memset(answer, 0, sizeof(*answer));
	answer->page_count = 1;

	while (offset < used) {
		size_t start, word_length;
		while (offset < used && normalized[offset] == ' ')
			offset++;
		start = offset;
		while (offset < used && normalized[offset] != ' ')
			offset++;
		word_length = offset - start;
		if (column != 0 && column + 1 + word_length > WORKSHOP_ANSWER_COLUMNS) {
			if (next_line(answer, &page, &line, &column) == -1)
				return -1;
		}
		if (column != 0)
			answer->pages[page][line][column++] = ' ';
		while (word_length != 0) {
			size_t remaining = WORKSHOP_ANSWER_COLUMNS - column;
			size_t take = word_length < remaining ? word_length : remaining;
			memcpy(answer->pages[page][line] + column, normalized + start, take);
			column += take;
			answer->pages[page][line][column] = '\0';
			start += take;
			word_length -= take;
			if (word_length != 0 && next_line(answer, &page, &line, &column) == -1)
				return -1;
		}
	}
	return 0;
}

int
workshop_answer_parse_response(const char *text, size_t length,
	struct workshop_answer *answer)
{
	size_t body_length;

	if (text == NULL || answer == NULL || length == 0) {
		errno = EINVAL;
		return -1;
	}
	if (length > WORKSHOP_ANSWER_MAX_BYTES + 2) {
		errno = EFBIG;
		return -1;
	}
	if (text[length - 1] != '\n') {
		errno = EILSEQ;
		return -1;
	}
	body_length = length - 1;
	if (body_length != 0 && text[body_length - 1] == '\r')
		body_length--;
	if (body_length > WORKSHOP_ANSWER_MAX_BYTES) {
		errno = EFBIG;
		return -1;
	}
	if (memchr(text, '\n', body_length) != NULL ||
	    memchr(text, '\r', body_length) != NULL) {
		errno = EILSEQ;
		return -1;
	}
	return workshop_answer_parse(text, body_length, answer);
}
