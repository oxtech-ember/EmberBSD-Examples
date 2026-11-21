/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "eqml.h"

#ifndef SCENE_SYMBOL
#error "define SCENE_SYMBOL to the generated eqml_<name> symbol"
#endif

extern const struct eqml_scene SCENE_SYMBOL;

struct output {
	FILE *stream;
};

static int
write_row(void *context, uint16_t y, const uint16_t *pixels, uint16_t width)
{
	struct output *output = context;
	unsigned char bytes[320 * 2];
	size_t i;
	(void)y;
	for (i = 0; i < width; i++) {
		bytes[i * 2] = (unsigned char)pixels[i];
		bytes[i * 2 + 1] = (unsigned char)(pixels[i] >> 8);
	}
	if (fwrite(bytes, sizeof(bytes[0]), (size_t)width * 2, output->stream) !=
	    (size_t)width * 2) {
		errno = EIO;
		return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	const struct eqml_scene *scene = &SCENE_SYMBOL;
	struct output output;
	char *end;
	unsigned long expected_width;
	unsigned long expected_height;
	uint16_t line[320];
	int status;

	if (argc != 4) {
		fprintf(stderr, "usage: render_generated output.rgb565 width height\n");
		return 2;
	}
	expected_width = strtoul(argv[2], &end, 10);
	if (*argv[2] == '\0' || *end != '\0' || expected_width > 320) {
		fprintf(stderr, "invalid expected width\n");
		return 2;
	}
	expected_height = strtoul(argv[3], &end, 10);
	if (*argv[3] == '\0' || *end != '\0' || expected_height > UINT16_MAX) {
		fprintf(stderr, "invalid expected height\n");
		return 2;
	}
	if (scene->width != expected_width || scene->height != expected_height ||
	    scene->property_count != 0) {
		fprintf(stderr, "unexpected scene shape: %ux%u properties=%zu expected=%lux%lu/0\n",
		    scene->width, scene->height, scene->property_count,
		    expected_width, expected_height);
		return 2;
	}
	output.stream = fopen(argv[1], "wb");
	if (output.stream == NULL) {
		perror("fopen");
		return 2;
	}
	status = eqml_render(scene, NULL, line, sizeof(line) / sizeof(line[0]),
	    write_row, &output);
	if (fclose(output.stream) != 0) {
		perror("fclose");
		return 2;
	}
	if (status != 0) {
		perror("eqml_render");
		return 1;
	}
	fprintf(stderr, "scene=%ux%u properties=%zu items=%zu rgb565=%ux%u\n",
	    scene->width, scene->height, scene->property_count, scene->item_count,
	    scene->width, scene->height);
	return 0;
}
