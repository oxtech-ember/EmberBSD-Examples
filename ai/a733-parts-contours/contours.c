/* SPDX-License-Identifier: MIT */

#include <sys/ioctl.h>

#include <arm/a733_npu_tile.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SIDE 8
#define POSITIONS (SIDE * SIDE)
#define MAX_SIDE 4096
#define PANEL_WIDTH 720
#define PANEL_HEIGHT 540
#define PANEL_LEFT 190
#define PANEL_RIGHT 540
#define PANEL_TOP 95
#define PANEL_BOTTOM 445

_Static_assert(A733_NPU_TILE_PIXELS == POSITIONS, "Unexpected tile size");
_Static_assert(A733_NPU_TILE_INPUTS == 4, "Unexpected input shape");
_Static_assert(A733_NPU_TILE_OUTPUTS == 8, "Unexpected output shape");

static size_t
dimension(const char *arg)
{
	char *end;
	unsigned long n;

	errno = 0;
	n = strtoul(arg, &end, 10);
	if (errno != 0 || end == arg || *end != '\0' || n == 0 || n > MAX_SIDE)
		return 0;
	return n;
}

static int
panel_threshold(const uint8_t *frame)
{
	unsigned int histogram[256] = {0};
	const unsigned int count =
	    (PANEL_RIGHT - PANEL_LEFT) * (PANEL_BOTTOM - PANEL_TOP);
	unsigned int total = 0, low_count = 0, low_sum = 0;
	double best = -1;
	int threshold = -1;

	for (size_t y = PANEL_TOP; y < PANEL_BOTTOM; y++) {
		for (size_t x = PANEL_LEFT; x < PANEL_RIGHT; x++)
			histogram[frame[y * PANEL_WIDTH + x]]++;
	}
	for (unsigned int i = 0; i < 256; i++)
		total += i * histogram[i];

	/* Otsu's between-class variance, with a common factor omitted. */
	for (unsigned int i = 0; i < 255; i++) {
		low_count += histogram[i];
		low_sum += i * histogram[i];
		if (low_count == 0 || low_count == count)
			continue;

		double delta = (double)low_sum * count - (double)total * low_count;
		double score = delta * delta / ((double)low_count * (count - low_count));

		if (score > best) {
			best = score;
			threshold = (int)i;
		}
	}
	return threshold;
}

static void
prepare_panel(uint8_t *frame, int threshold)
{
	for (size_t y = 0; y < PANEL_HEIGHT; y++) {
		for (size_t x = 0; x < PANEL_WIDTH; x++) {
			size_t p = y * PANEL_WIDTH + x;
			int in_panel = x >= PANEL_LEFT && x < PANEL_RIGHT &&
			    y >= PANEL_TOP && y < PANEL_BOTTOM;

			frame[p] = in_panel && frame[p] > threshold ? 255 : 0;
		}
	}
}

static uint8_t
quantize(uint8_t value)
{
	return 123 + (value * 10 + 127) / 255;
}

static void
load_tile(struct a733_npu_tile *tile, const uint8_t *frame, size_t width,
    size_t height, size_t left, size_t top)
{
	for (size_t j = 0; j < SIDE; j++) {
		size_t y = top + j < height ? top + j : height - 1;
		size_t below = y + 1 < height ? y + 1 : y;

		for (size_t i = 0; i < SIDE; i++) {
			size_t x = left + i < width ? left + i : width - 1;
			size_t right = x + 1 < width ? x + 1 : x;
			size_t p = j * SIDE + i;

			tile->input[0][p] = quantize(frame[y * width + x]);
			tile->input[1][p] = quantize(frame[y * width + right]);
			tile->input[2][p] = quantize(frame[below * width + right]);
			tile->input[3][p] = quantize(frame[below * width + x]);
		}
	}
}

static int
check_tile(const struct a733_npu_tile *tile)
{
	for (size_t c = 0; c < 4; c++) {
		for (size_t p = 0; p < POSITIONS; p++) {
			int value = tile->input[c][p];
			int next = tile->input[(c + 1) % 4][p];
			int direct = value + (int)c + 1;
			int difference = 128 + value - next + (int)c + 5;

			if (tile->output[c][p] != direct ||
			    tile->output[c + 4][p] != difference) {
				fprintf(stderr, "Tensor mismatch at plane %zu, index %zu\n", c, p);
				return -1;
			}
		}
	}
	return 0;
}

static size_t
render_tile(const struct a733_npu_tile *tile, uint8_t *frame, size_t width,
    size_t height, size_t left, size_t top)
{
	size_t count = 0;

	for (size_t j = 0; j < SIDE && top + j < height; j++) {
		for (size_t i = 0; i < SIDE && left + i < width; i++) {
			size_t p = j * SIDE + i;
			int dx = (int)tile->output[4][p] - 133;
			int dy = (int)tile->output[7][p] - 136;

			frame[(top + j) * width + left + i] =
			    abs(dx) + abs(dy) >= 3 ? 255 : 0;
			count++;
		}
	}
	return count;
}

int
main(int argc, char **argv)
{
	size_t width, height, bytes, frames = 0, positions = 0, tiles = 0;
	uint8_t *input = NULL, *output = NULL;
	int dev = -1, status = EXIT_FAILURE, threshold = -1;
	int first = 1, panel = 0;
	FILE *source = stdin, *file = NULL;

	if (argc > 1 && strcmp(argv[1], "--tless-panel") == 0) {
		panel = 1;
		first++;
	}
	if (argc < first + 2 || argc > first + 3 ||
	    (width = dimension(argv[first])) == 0 ||
	    (height = dimension(argv[first + 1])) == 0 ||
	    (panel && (width != PANEL_WIDTH || height != PANEL_HEIGHT))) {
		fprintf(stderr,
		    "usage: %s [--tless-panel] width height [gray-file]\n",
		    argv[0]);
		return EXIT_FAILURE;
	}
	bytes = width * height;
	if (argc == first + 3) {
		file = fopen(argv[first + 2], "rb");
		if (file == NULL) {
			perror(argv[first + 2]);
			return EXIT_FAILURE;
		}
		source = file;
	}
	input = malloc(bytes);
	output = malloc(bytes);
	if (input == NULL || output == NULL) {
		perror("malloc");
		goto done;
	}
	dev = open("/dev/vipcore", O_RDWR);
	if (dev == -1) {
		perror("/dev/vipcore");
		goto done;
	}
	for (;;) {
		size_t received = fread(input, 1, bytes, source);
		size_t current = 0;

		if (received == 0 && feof(source)) {
			if (frames != 0)
				status = EXIT_SUCCESS;
			else
				fprintf(stderr, "No complete input frames\n");
			break;
		}
		if (received != bytes) {
			fprintf(stderr, "Incomplete input frame %zu (%zu/%zu bytes)\n",
			    frames, received, bytes);
			break;
		}
		if (panel) {
			if (threshold < 0) {
				threshold = panel_threshold(input);
				if (threshold < 0) {
					fprintf(stderr, "Panel calibration needs two intensity classes\n");
					break;
				}
				fprintf(stderr, "panel_threshold=%d roi=190,95,350,350\n",
				    threshold);
			}
			prepare_panel(input, threshold);
		}
		for (size_t top = 0; top < height; top += SIDE) {
			for (size_t left = 0; left < width; left += SIDE) {
				struct a733_npu_tile tile = {0};
				uint8_t submitted[sizeof(tile.input)];

				load_tile(&tile, input, width, height, left, top);
				memcpy(submitted, tile.input, sizeof(submitted));
				if (ioctl(dev, A733_NPU_TILE_RUN, &tile) == -1) {
					if (errno == EALREADY)
						fprintf(stderr, "Fixed task exhausted at frame %zu (%zu/%zu positions verified)\n",
						    frames, current, bytes);
					else
						perror("A733_NPU_TILE_RUN");
					goto done;
				}
				if (memcmp(submitted, tile.input, sizeof(submitted)) != 0) {
					fprintf(stderr, "The ioctl changed the input tensor\n");
					goto done;
				}
				if (check_tile(&tile) != 0)
					goto done;
				size_t count = render_tile(&tile, output, width,
				    height, left, top);
				current += count;
				positions += count;
				tiles++;
			}
		}
		if (fwrite(output, 1, bytes, stdout) != bytes || fflush(stdout) != 0) {
			perror("stdout");
			break;
		}
		frames++;
	}

done:
	fprintf(stderr, "frames_written=%zu positions_verified=%zu tiles_verified=%zu\n",
	    frames, positions, tiles);
	if (dev != -1)
		close(dev);
	if (file != NULL)
		fclose(file);
	free(output);
	free(input);
	return status;
}
