/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../answer.h"

int
main(void)
{
	struct workshop_answer answer;
	const char short_text[] = "Check the latch.\nLook for a crack.";
	char long_text[WORKSHOP_ANSWER_MAX_BYTES + 1];
	const char russian[] = "Ответ по детали.";

	assert(workshop_answer_parse(short_text, sizeof(short_text) - 1, &answer) == 0);
	assert(answer.page_count == 1);
	assert(strcmp(answer.pages[0][0], "Check the latch. Look") == 0);
	assert(strcmp(answer.pages[0][1], "for a crack.") == 0);
	assert(workshop_answer_parse_response("Check the latch.\n", 17, &answer) == 0);
	assert(strcmp(answer.pages[0][0], "Check the latch.") == 0);
	errno = 0;
	assert(workshop_answer_parse_response("two\nlines\n", 10, &answer) == -1);
	assert(errno == EILSEQ);
	errno = 0;
	assert(workshop_answer_parse_response("missing newline", 15, &answer) == -1);
	assert(errno == EILSEQ);
	assert(workshop_answer_parse(russian, sizeof(russian) - 1, &answer) == -1);
	assert(errno == EILSEQ);
	assert(workshop_answer_parse("\n\t", 2, &answer) == -1);
	assert(errno == EINVAL);

	memset(long_text, 'A', sizeof(long_text));
	assert(workshop_answer_parse(long_text, WORKSHOP_ANSWER_MAX_BYTES, &answer) == 0);
	assert(answer.page_count == WORKSHOP_ANSWER_MAX_PAGES);
	for (size_t p = 0; p < answer.page_count; p++)
		for (size_t line = 0; line < WORKSHOP_ANSWER_LINES; line++)
			assert(strlen(answer.pages[p][line]) <= WORKSHOP_ANSWER_COLUMNS);
	assert(workshop_answer_parse(long_text, sizeof(long_text), &answer) == -1);
	assert(errno == EFBIG);
	char response[WORKSHOP_ANSWER_MAX_BYTES + 3];
	memset(response, 'A', sizeof(response));
	response[WORKSHOP_ANSWER_MAX_BYTES] = '\n';
	assert(workshop_answer_parse_response(response,
	    WORKSHOP_ANSWER_MAX_BYTES + 1, &answer) == 0);
	response[WORKSHOP_ANSWER_MAX_BYTES] = '\r';
	response[WORKSHOP_ANSWER_MAX_BYTES + 1] = '\n';
	assert(workshop_answer_parse_response(response,
	    WORKSHOP_ANSWER_MAX_BYTES + 2, &answer) == 0);
	response[WORKSHOP_ANSWER_MAX_BYTES] = 'A';
	response[WORKSHOP_ANSWER_MAX_BYTES + 1] = '\r';
	response[WORKSHOP_ANSWER_MAX_BYTES + 2] = '\n';
	assert(workshop_answer_parse_response(response,
	    WORKSHOP_ANSWER_MAX_BYTES + 3, &answer) == -1);
	assert(errno == EFBIG);
	puts("answer formatter: PASS");
	return 0;
}
