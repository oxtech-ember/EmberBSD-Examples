/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the actual renderer and event callbacks with memory and a pipe. */
#define main example_main
#include "../main.c"
#undef main
#include <assert.h>

int
main(void)
{
	uint16_t framebuffer[8 * 4] = {0};
	uint16_t words[] = {1, 2, 3, 4};
	uint8_t pixels[sizeof(words)];
	lv_area_t area = {2, 1, 3, 2};
	lv_indev_data_t data = {0};
	struct wscons_event events[4] = {
	    {.type = WSCONS_EVENT_MOUSE_DOWN, .value = 0},
	    {.type = WSCONS_EVENT_MOUSE_ABSOLUTE_X, .value = 25},
	    {.type = WSCONS_EVENT_MOUSE_ABSOLUTE_Y, .value = 31},
	    {.type = WSCONS_EVENT_MOUSE_UP, .value = 0}};
	lv_display_t *display;
	struct wsdisplay_fbinfo info = {.width = 64, .height = 64, .depth = 16};
	struct wsdisplayio_fbinfo fbinfo = {
	    .fbi_width = 64, .fbi_height = 64, .fbi_stride = 128,
	    .fbi_bitsperpixel = 16, .fbi_pixeltype = WSFB_RGB,
	    .fbi_fbsize = 8192,
	    .fbi_subtype.fbi_rgbmasks = {11, 5, 5, 6, 0, 5, 0, 0}
	};
	int fds[2], i;

	assert(valid_framebuffer(&info, &fbinfo, 128));
	fbinfo.fbi_subtype.fbi_rgbmasks.red_offset = 0;
	assert(!valid_framebuffer(&info, &fbinfo, 128));
	fbinfo.fbi_subtype.fbi_rgbmasks.red_offset = 11;
	fbinfo.fbi_fboffset = 1;
	assert(!valid_framebuffer(&info, &fbinfo, 128));

	lv_init();
	display = lv_display_create(64, 64);
	assert(display != NULL);
	app.status = lv_label_create(lv_screen_active());
	assert(app.status != NULL);
	app.fb = (uint8_t *)framebuffer;
	app.stride = 8 * sizeof(uint16_t);
	app.width = 64;
	app.height = 64;
	memcpy(pixels, words, sizeof(pixels));
	flush(display, &area, pixels);
	assert(framebuffer[10] == 1 && framebuffer[11] == 2);
	assert(framebuffer[18] == 3 && framebuffer[19] == 4);
	assert(framebuffer[9] == 0 && framebuffer[12] == 0);
	assert(pipe(fds) == 0);
	assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
	app.mouse_fd = fds[0];
	assert(write(fds[1], events, sizeof(events)) ==
	       (ssize_t)sizeof(events));
	for (i = 0; i < 3; i++) {
		memset(&data, 0, sizeof(data));
		read_mouse(NULL, &data);
	}
	assert(data.point.x == 25 && data.point.y == 31);
	assert(data.state == LV_INDEV_STATE_PRESSED && data.continue_reading);
	assert(strcmp(lv_label_get_text(app.status), "Touch: 25, 31") == 0);
	memset(&data, 0, sizeof(data));
	read_mouse(NULL, &data);
	assert(data.state == LV_INDEV_STATE_RELEASED);
	memset(&data, 0, sizeof(data));
	read_mouse(NULL, &data);
	assert(!data.continue_reading && !app.input_error);
	assert(clamp(-1, 64) == 0 && clamp(64, 64) == 63);
	close(fds[0]);
	close(fds[1]);
	lv_deinit();
	puts("PASS framebuffer row stride and wscons pointer events");
	return 0;
}
