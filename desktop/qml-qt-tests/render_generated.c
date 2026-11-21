/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	if (width == 0)
		return 0;
	if (width > sizeof(bytes) / 2) {
		errno = EOVERFLOW;
		return -1;
	}
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
	struct eqml_scene scene = SCENE_SYMBOL;
	struct eqml_scene resized_scene;
	const struct eqml_scene *render_scene = &scene;
	struct output output;
	int32_t values[16];
	char *end;
	unsigned long expected_width;
	unsigned long expected_height;
	uint16_t line[320];
	int status;
	int resize_root;

	if (argc != 4 && argc != 5) {
		fprintf(stderr, "usage: render_generated output.rgb565 width height [--resize-root]\n");
		return 2;
	}
	resize_root = argc == 5 && strcmp(argv[4], "--resize-root") == 0;
	if (argc == 5 && !resize_root) {
		fprintf(stderr, "unknown option: %s\n", argv[4]);
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
	if (scene.property_count > sizeof(values) / sizeof(values[0]) ||
	    (scene.property_count != 0 && scene.properties == NULL)) {
		fprintf(stderr, "unsupported scene properties\n");
		return 2;
	}
	if (resize_root) {
		if (expected_width > scene.width || expected_height > scene.height) {
			fprintf(stderr, "resized root must fit within the compiled scene\n");
			return 2;
		}
		resized_scene = scene;
		resized_scene.width = (uint16_t)expected_width;
		resized_scene.height = (uint16_t)expected_height;
		render_scene = &resized_scene;
	} else if (scene.width != expected_width || scene.height != expected_height ||
	    scene.property_count != 0) {
		fprintf(stderr, "unexpected scene shape: %ux%u properties=%zu expected=%lux%lu/0\n",
		    scene.width, scene.height, scene.property_count,
		    expected_width, expected_height);
		return 2;
	}
	for (size_t i = 0; i < scene.property_count; i++)
		values[i] = scene.properties[i].initial;
	output.stream = fopen(argv[1], "wb");
	if (output.stream == NULL) {
		perror("fopen");
		return 2;
	}
	status = eqml_render(render_scene, scene.property_count == 0 ? NULL : values,
	    line, sizeof(line) / sizeof(line[0]), write_row, &output);
	if (fclose(output.stream) != 0) {
		perror("fclose");
		return 2;
	}
	if (status != 0) {
		perror("eqml_render");
		return 1;
	}
	fprintf(stderr, "scene=%ux%u properties=%zu items=%zu rgb565=%lux%lu\n",
	    scene.width, scene.height, scene.property_count, scene.item_count,
	    expected_width, expected_height);
	return 0;
}
