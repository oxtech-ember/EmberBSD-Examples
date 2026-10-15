/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
number(const char *text, unsigned int minimum, unsigned int maximum,
    unsigned int *result)
{
	char *end = NULL;
	uintmax_t value;

	if (text[0] < '0' || text[0] > '9')
		return 0;
	errno = 0;
	value = strtoumax(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || value < minimum ||
	    value > maximum)
		return 0;
	*result = (unsigned int)value;
	return 1;
}

static int
write_all(const unsigned char *data, size_t length)
{

	while (length != 0) {
		const size_t count = fwrite(data, 1, length, stdout);
		if (count != 0) {
			data += count;
			length -= count;
			continue;
		}
		if (ferror(stdout) && errno == EINTR) {
			clearerr(stdout);
			continue;
		}
		fprintf(stderr, "Cannot write I420 data: %s\n",
		    ferror(stdout) ? strerror(errno) : "zero-length write");
		return 0;
	}
	return 1;
}

int
main(int argc, char **argv)
{
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int frames = 0;
	unsigned int channel = 0;
	unsigned char *luma;
	unsigned char *chroma;
	int success = 1;

	if (argc != 5 || !number(argv[1], 16, 32768, &width) ||
	    !number(argv[2], 16, 32768, &height) ||
	    !number(argv[3], 2, 256, &frames) ||
	    !number(argv[4], 0, 3, &channel) || width % 2 != 0 ||
	    height % 2 != 0) {
		fprintf(stderr, "Usage: i420-fixture WIDTH HEIGHT FRAMES CHANNEL\n"
		    "Use even dimensions 16..32768, frames 2..256, channel 0..3.\n");
		return 2;
	}
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		fprintf(stderr, "Cannot configure SIGPIPE: %s\n", strerror(errno));
		return 1;
	}
	luma = malloc((size_t)width);
	chroma = malloc((size_t)(width / 2));
	if (luma == NULL || chroma == NULL) {
		fprintf(stderr, "Cannot allocate fixture rows\n");
		free(luma);
		free(chroma);
		return 1;
	}
	memset(chroma, 128, (size_t)(width / 2));
	for (unsigned int frame = 0; success && frame < frames; ++frame) {
		for (unsigned int row = 0; success && row < height; ++row) {
			if (row % 64 == 0) {
				for (unsigned int column = 0; column < width; column += 64) {
					const unsigned char value = (unsigned char)(16 +
					    ((column / 64 + 3 * (row / 64) +
					    5 * frame + 41 * channel) % 220));
					const unsigned int count = width - column < 64
					    ? width - column : 64;
					memset(luma + column, value, (size_t)count);
				}
			}
			if (row == 0 || row == 16) {
				for (unsigned int column = 0; column < 16; ++column) {
					luma[column] = row == 0
					    ? (unsigned char)((frame & (1U << (column / 2))) ? 235 : 16)
					    : (unsigned char)(16 + ((5 * frame + 41 * channel) % 220));
				}
			}
			success = write_all(luma, (size_t)width);
		}
		for (unsigned int plane = 0; success && plane < 2; ++plane) {
			for (unsigned int row = 0; success && row < height / 2; ++row)
				success = write_all(chroma, (size_t)(width / 2));
		}
	}
	if (success && fflush(stdout) == EOF) {
		fprintf(stderr, "Cannot flush I420 data: %s\n", strerror(errno));
		success = 0;
	}
	free(luma);
	free(chroma);
	return success ? 0 : 1;
}
