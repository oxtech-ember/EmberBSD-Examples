/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "eqml_input.h"
#include "sparkworkshop.h"
#include "view.h"

static const char *last_signal;

static void
signal_seen(void *context, const char *name)
{
	(void)context;
	last_signal = name;
}

static void
click(struct eqml_ui *ui, int32_t x, int32_t y)
{
	assert(eqml_ui_pointer(ui, EQML_DOWN, x, y, EQML_BUTTON_LEFT, 0, 1000) >= 0);
	assert(eqml_ui_pointer(ui, EQML_UP, x, y, EQML_BUTTON_LEFT, 0, 1010) >= 0);
}

int
main(void)
{
	static const struct workshop_guide guide = {
		.title = "Connector inspection",
		.step_count = 3,
		.steps = {
			{ .title = "Find the marking", .line_count = 1,
			  .lines = { "Place the connector on the bench." } },
			{ .title = "Inspect the latch", .line_count = 2,
			  .lines = { "Find the locking tab. Take a photo", "that shows the tip and its base." } },
			{ .title = "Check the housing", .line_count = 1,
			  .lines = { "Look for cracks along every edge." } }
		}
	};
	static uint16_t pixels[160 * 120];
	const struct eqml_bitmap bitmap = { 160, 120, pixels, 160 * 120, NULL, 0 };
	struct eqml_ui *ui = eqml_ui_create(&eqml_sparkworkshop, NULL, NULL);
	assert(ui != NULL);
	eqml_ui_set_signal_handler(ui, signal_seen, NULL);
	assert(workshop_view_home(ui, &guide) == 0);
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_homePage));
	assert(!eqml_ui_visible(ui, eqml_sparkworkshop_node_lessonPage));
	assert(strcmp(eqml_ui_string(ui, eqml_sparkworkshop_text_guideTitle), guide.title) == 0);
	assert(workshop_view_step(ui, &guide, 1, NULL) == 0);
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_lessonPage));
	assert(strcmp(eqml_ui_string(ui, eqml_sparkworkshop_text_stepTitle), "Inspect the latch") == 0);
	assert(strcmp(eqml_ui_string(ui, eqml_sparkworkshop_text_lessonCount), "2 / 3") == 0);
	click(ui, 220, 370);
	assert(strcmp(last_signal, "cameraRequested") == 0);
	assert(workshop_view_camera(ui, 1, "READY / TAKE A PHOTO", NULL, 0) == 0);
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_cameraPage));
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_captureAction));
	assert(!eqml_ui_visible(ui, eqml_sparkworkshop_node_useAction));
	click(ui, 400, 118);
	assert(strcmp(last_signal, "cameraCancelRequested") == 0);
	assert(workshop_view_camera(ui, 1, "REVIEW FRAME", &bitmap, 1) == 0);
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_cameraPreview));
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_retakeAction));
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_useAction));
	assert(!eqml_ui_visible(ui, eqml_sparkworkshop_node_captureAction));
	click(ui, 100, 394);
	assert(strcmp(last_signal, "cameraRetakeRequested") == 0);
	click(ui, 300, 394);
	assert(strcmp(last_signal, "cameraUseRequested") == 0);
	assert(workshop_view_step(ui, &guide, 1, &bitmap) == 0);
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_photoThumbnail));
	assert(strcmp(eqml_ui_string(ui, eqml_sparkworkshop_text_photoHint),
	    "PHOTO ATTACHED TO THIS STEP") == 0);
	assert(workshop_view_step(ui, &guide, 2, NULL) == 0);
	assert(!eqml_ui_visible(ui, eqml_sparkworkshop_node_photoThumbnail));
	assert(workshop_view_step(ui, &guide, 1, &bitmap) == 0);
	assert(eqml_ui_visible(ui, eqml_sparkworkshop_node_photoThumbnail));
	eqml_ui_destroy(ui);
	assert(puts("QML view/input: PASS") >= 0);
	return 0;
}
