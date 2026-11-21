/* SPDX-License-Identifier: BSD-2-Clause */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "sparkworkshop.h"
#include "view.h"

static int
visible(struct eqml_ui *ui, size_t node, int value)
{
	return eqml_ui_set_visible(ui, node, value);
}

int
workshop_view_home(struct eqml_ui *ui, const struct workshop_guide *guide)
{
	char count[32];
	if (ui == NULL || guide == NULL || guide->step_count == 0) {
		errno = EINVAL;
		return -1;
	}
	snprintf(count, sizeof(count), "%u STEPS", guide->step_count);
	if (eqml_ui_set(ui, eqml_sparkworkshop_text_guideTitle, guide->title) == -1 ||
	    eqml_ui_set(ui, eqml_sparkworkshop_text_homeStepCount, count) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_homePage, 1) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_lessonPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_cameraPage, 0) == -1)
		return -1;
	return 0;
}

int
workshop_view_step(struct eqml_ui *ui, const struct workshop_guide *guide,
	unsigned int index, const struct eqml_bitmap *photo)
{
	const struct workshop_step *step;
	char number[24];
	const char *line[WORKSHOP_TEXT_LINES] = { "", "", "" };
	if (ui == NULL || guide == NULL || index >= guide->step_count) {
		errno = EINVAL;
		return -1;
	}
	step = &guide->steps[index];
	snprintf(number, sizeof(number), "%u / %u", index + 1, guide->step_count);
	for (unsigned int i = 0; i < step->line_count; i++)
		line[i] = step->lines[i];
	if (eqml_ui_set(ui, eqml_sparkworkshop_text_lessonCount, number) == -1 ||
	    eqml_ui_set(ui, eqml_sparkworkshop_text_stepTitle, step->title) == -1 ||
	    eqml_ui_set(ui, eqml_sparkworkshop_text_stepBody1, line[0]) == -1 ||
	    eqml_ui_set(ui, eqml_sparkworkshop_text_stepBody2, line[1]) == -1 ||
	    eqml_ui_set(ui, eqml_sparkworkshop_text_stepBody3, line[2]) == -1 ||
	    eqml_ui_set(ui, eqml_sparkworkshop_text_photoHint,
	    photo != NULL ? "PHOTO ATTACHED TO THIS STEP" : "NO PHOTO ATTACHED") == -1 ||
	    eqml_ui_set_bitmap(ui, eqml_sparkworkshop_node_photoThumbnail, photo) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_photoThumbnail, photo != NULL) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_nextAction,
	    index + 1 < guide->step_count) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_doneAction,
	    index + 1 == guide->step_count) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_homePage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_cameraPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_lessonPage, 1) == -1)
		return -1;
	if (visible(ui, eqml_sparkworkshop_node_askAction, photo != NULL) == -1)
		return -1;
	return 0;
}

int
workshop_view_camera(struct eqml_ui *ui, unsigned int index,
	const char *status, const struct eqml_bitmap *preview, int can_review)
{
	char step[24];
	if (ui == NULL || status == NULL) {
		errno = EINVAL;
		return -1;
	}
	snprintf(step, sizeof(step), "STEP %u", index + 1);
	if (eqml_ui_set(ui, eqml_sparkworkshop_text_cameraStep, step) == -1 ||
	    eqml_ui_set(ui, eqml_sparkworkshop_text_cameraStatus, status) == -1 ||
	    eqml_ui_set_bitmap(ui, eqml_sparkworkshop_node_cameraPreview, preview) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_cameraPreview, preview != NULL) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_captureAction, !can_review) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_retakeAction, can_review) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_useAction, can_review) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_homePage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_lessonPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_cameraPage, 1) == -1)
		return -1;
	return 0;
}

int
workshop_view_answer(struct eqml_ui *ui, const struct eqml_bitmap *photo,
	enum workshop_answer_state state, size_t page, size_t page_count,
	const char lines[WORKSHOP_ANSWER_LINES][WORKSHOP_ANSWER_LINE_BYTES])
{
	char count[24];
	const size_t text_nodes[WORKSHOP_ANSWER_LINES] = {
		eqml_sparkworkshop_text_answerLine1, eqml_sparkworkshop_text_answerLine2,
		eqml_sparkworkshop_text_answerLine3, eqml_sparkworkshop_text_answerLine4,
		eqml_sparkworkshop_text_answerLine5, eqml_sparkworkshop_text_answerLine6,
		eqml_sparkworkshop_text_answerLine7
	};

	if (ui == NULL || photo == NULL || lines == NULL ||
	    state > WORKSHOP_ANSWER_ERROR ||
	    (state == WORKSHOP_ANSWER_READY && (page_count == 0 || page >= page_count))) {
		errno = EINVAL;
		return -1;
	}
	if (state == WORKSHOP_ANSWER_WAITING)
		snprintf(count, sizeof(count), "WAITING");
	else if (state == WORKSHOP_ANSWER_ERROR)
		snprintf(count, sizeof(count), "ERROR");
	else
		snprintf(count, sizeof(count), "%u / %u", (unsigned int)page + 1,
		    (unsigned int)page_count);
	if (eqml_ui_set(ui, eqml_sparkworkshop_text_answerPageCount, count) == -1 ||
	    eqml_ui_set_bitmap(ui, eqml_sparkworkshop_node_answerPhoto, photo) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerPhoto, 1) == -1)
		return -1;
	for (size_t i = 0; i < WORKSHOP_ANSWER_LINES; i++)
		if (eqml_ui_set(ui, text_nodes[i], lines[i]) == -1)
			return -1;
	if (visible(ui, eqml_sparkworkshop_node_answerPreviousAction,
	    state == WORKSHOP_ANSWER_READY && page != 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerBackAction,
	    state != WORKSHOP_ANSWER_WAITING) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerCancelAction,
	    state == WORKSHOP_ANSWER_WAITING) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerNextAction,
	    state == WORKSHOP_ANSWER_READY && page + 1 < page_count) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerRetryAction,
	    state == WORKSHOP_ANSWER_ERROR) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_homePage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_lessonPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_cameraPage, 0) == -1 ||
	    visible(ui, eqml_sparkworkshop_node_answerPage, 1) == -1)
		return -1;
	return 0;
}
