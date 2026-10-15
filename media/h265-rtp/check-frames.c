/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */

/* An independent mathematical I420 oracle. No media library is linked. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 96U
#define HEIGHT 64U
#define FPS 10U
#define GOP_FRAMES 4U
#define FRAME_BYTES (WIDTH * HEIGHT * 3U / 2U)
#define MAX_FRAMES 4096U

struct options {
	const char *path;
	uint32_t frames, require_count, tail_start, tail_count;
	int allow_damaged, has_count, has_tail_start, has_tail_count;
};

struct damage {
	uint32_t input_index, claimed_id, offset, actual, expected;
	int header_valid;
};

struct result {
	uint32_t input_frames, exact_frames, damaged_frames, order_errors;
	uint32_t tail_exact_frames, tail_next, truncated_bytes;
	uint32_t exact_ids[MAX_FRAMES];
	struct damage damage[MAX_FRAMES];
	const char *failure;
};

static void
usage(void)
{
	fputs("Usage: check-frames RAW.i420 [--frames N] [--require-count N]\n"
	    "  [--allow-damaged --require-tail-start N --require-tail-count M]\n"
	    "Fixed I420: 96x64, 10 fps, frame marker E35F and uint16 BE id.\n"
	    "Default frames=40. All exact IDs must strictly increase. Strict mode\n"
	    "requires IDs from zero and defaults require-count to frames.\n"
	    "Loss mode requires N+M=frames and N on a GOP-4 boundary. Every tail\n"
	    "frame N..frames-1 must have exact pixels in order. Damaged frames\n"
	    "are allowed only before that tail starts. Counts are bounded4096.\n", stderr);
}

static int
number(const char *s, uint32_t minimum, uint32_t *result)
{
	uint32_t value = 0, digit;

	if (*s == '\0')
		return -1;
	for (; *s != '\0'; s++) {
		if (*s < '0' || *s > '9')
			return -1;
		digit = (uint32_t)(*s - '0');
		if (value > (MAX_FRAMES - digit) / 10U)
			return -1;
		value = 10U * value + digit;
	}
	if (value < minimum || value > MAX_FRAMES)
		return -1;
	*result = value;
	return 0;
}

static int
options(int argc, char **argv, struct options *o)
{
	unsigned int seen = 0, bit;
	int i;
	uint32_t value;

	memset(o, 0, sizeof(*o));
	o->frames = 40;
	if (argc < 2 || argv[1][0] == '-')
		return -1;
	o->path = argv[1];
	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--allow-damaged") == 0) {
			if (o->allow_damaged)
				return -1;
			o->allow_damaged = 1;
			continue;
		}
		if (strcmp(argv[i], "--frames") == 0) bit = 1U;
		else if (strcmp(argv[i], "--require-count") == 0) bit = 2U;
		else if (strcmp(argv[i], "--require-tail-start") == 0) bit = 4U;
		else if (strcmp(argv[i], "--require-tail-count") == 0) bit = 8U;
		else return -1;
		if ((seen & bit) != 0 || ++i == argc ||
		    number(argv[i], bit == 4U ? 0U : 1U, &value) != 0)
			return -1;
		seen |= bit;
		if (bit == 1U) o->frames = value;
		else if (bit == 2U) { o->require_count = value; o->has_count = 1; }
		else if (bit == 4U) { o->tail_start = value; o->has_tail_start = 1; }
		else { o->tail_count = value; o->has_tail_count = 1; }
	}
	if (o->allow_damaged) {
		if (!o->has_tail_start || !o->has_tail_count ||
		    o->tail_start >= o->frames || o->tail_start % GOP_FRAMES != 0 ||
		    o->tail_count != o->frames - o->tail_start)
			return -1;
	} else {
		if (o->has_tail_start || o->has_tail_count)
			return -1;
		if (!o->has_count) {
			o->has_count = 1;
			o->require_count = o->frames;
		}
	}
	return 0;
}

static void
fail(struct result *r, const char *reason)
{
	if (r->failure == NULL)
		r->failure = reason;
}

static unsigned char
expected_byte(uint32_t offset, uint32_t id)
{
	uint32_t x, y;

	if (offset < 8) {
		switch (offset) {
		case 0: return 'E';
		case 1: return '3';
		case 2: return '5';
		case 3: return 'F';
		case 4: return (unsigned char)(id >> 8);
		case 5: return (unsigned char)id;
		case 6: return (unsigned char)(id ^ 0xa5U);
		default: return (unsigned char)(id + 0x5aU);
		}
	}
	if (offset < WIDTH * HEIGHT) {
		x = offset % WIDTH;
		y = offset / WIDTH;
		return (unsigned char)(17U * x + 31U * y + 13U * id +
		    (((x ^ y) * id) % 256U));
	}
	offset -= WIDTH * HEIGHT;
	if (offset < WIDTH * HEIGHT / 4U) {
		x = offset % (WIDTH / 2U);
		y = offset / (WIDTH / 2U);
		return (unsigned char)(73U + 7U * x + 19U * y + 5U * id);
	}
	offset -= WIDTH * HEIGHT / 4U;
	x = offset % (WIDTH / 2U);
	y = offset / (WIDTH / 2U);
	return (unsigned char)(151U + 11U * x + 3U * y + 17U * id);
}

static void
check_frame(const unsigned char *pixels, const struct options *o,
    struct result *r)
{
	uint32_t id, offset, previous, index;
	unsigned char expected = 0;
	struct damage *damage;
	int header_valid;

	index = r->input_frames;
	id = (uint32_t)pixels[4] * 256U + pixels[5];
	header_valid = memcmp(pixels, "E35F", 4) == 0 && id < o->frames &&
	    pixels[6] == (unsigned char)(id ^ 0xa5U) &&
	    pixels[7] == (unsigned char)(id + 0x5aU);
	offset = 0;
	if (header_valid) {
		for (offset = 0; offset < FRAME_BYTES; offset++) {
			expected = expected_byte(offset, id);
			if (pixels[offset] != expected)
				break;
		}
	}
	if (!header_valid || offset != FRAME_BYTES) {
		damage = &r->damage[r->damaged_frames++];
		damage->input_index = index;
		damage->claimed_id = id;
		damage->header_valid = header_valid;
		damage->offset = offset;
		damage->actual = pixels[offset];
		damage->expected = expected;
		if (!o->allow_damaged)
			fail(r, header_valid ? "pixel-mismatch" : "frame-header");
		else if (r->tail_exact_frames != 0)
			fail(r, "damaged-recovery-tail");
		return;
	}
	if (r->exact_frames != 0) {
		previous = r->exact_ids[r->exact_frames - 1];
		if (id <= previous) {
			r->order_errors++;
			fail(r, "frame-order");
			return;
		}
	}
	if (!o->allow_damaged && id != index) {
		r->order_errors++;
		fail(r, "frame-order");
		return;
	}
	r->exact_ids[r->exact_frames++] = id;
	if (o->allow_damaged && id >= o->tail_start) {
		if (id != r->tail_next) {
			fail(r, "recovery-tail-order");
			return;
		}
		r->tail_next++;
		r->tail_exact_frames++;
	}
}

static int
report(const struct options *o, const struct result *r)
{
	uint32_t i;
	const struct damage *d;

	if (printf("{\"ok\":%s,\"reason\":\"%s\",\"format\":\"I420\","
	    "\"width\":%u,\"height\":%u,\"fps_num\":%u,\"fps_den\":1,"
	    "\"gop_frames\":%u,\"configured_frames\":%" PRIu32 ","
	    "\"input_frames\":%" PRIu32 ",\"exact_frames\":%" PRIu32 ","
	    "\"damaged_frames\":%" PRIu32 ",\"order_errors\":%" PRIu32 ","
	    "\"truncated_bytes\":%" PRIu32 ",\"tail_start\":%" PRIu32 ","
	    "\"tail_required\":%" PRIu32 ",\"tail_exact\":%" PRIu32 ",\"exact_ids\":[",
	    r->failure == NULL ? "true" : "false", r->failure == NULL ? "pass" : r->failure,
	    WIDTH, HEIGHT, FPS, GOP_FRAMES, o->frames, r->input_frames, r->exact_frames,
	    r->damaged_frames, r->order_errors, r->truncated_bytes, o->tail_start,
	    o->tail_count, r->tail_exact_frames) < 0)
		return -1;
	for (i = 0; i < r->exact_frames; i++)
		if (printf("%s%" PRIu32, i == 0 ? "" : ",", r->exact_ids[i]) < 0)
			return -1;
	if (fputs("],\"damage\":[", stdout) == EOF)
		return -1;
	for (i = 0; i < r->damaged_frames; i++) {
		d = &r->damage[i];
		if (printf("%s{\"input_index\":%" PRIu32 ",\"claimed_id\":%" PRIu32
		    ",\"header_valid\":%s,\"first_offset\":%" PRIu32 ",\"actual\":%" PRIu32
		    ",\"expected\":%" PRIu32 "}", i == 0 ? "" : ",", d->input_index,
		    d->claimed_id, d->header_valid ? "true" : "false", d->offset,
		    d->actual, d->expected) < 0)
			return -1;
	}
	return fputs("]}\n", stdout) == EOF ? -1 : 0;
}

int
main(int argc, char **argv)
{
	struct options o;
	struct result r = {0};
	unsigned char pixels[FRAME_BYTES];
	size_t count;
	FILE *input;

	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		usage();
		return EXIT_SUCCESS;
	}
	if (options(argc, argv, &o) != 0) {
		usage();
		return 64;
	}
	r.tail_next = o.tail_start;
	input = fopen(o.path, "rb");
	if (input == NULL)
		fail(&r, "open-input");
	else {
		for (;;) {
			count = fread(pixels, 1, sizeof(pixels), input);
			if (count == 0) {
				if (ferror(input))
					fail(&r, "read-input");
				break;
			}
			if (count != sizeof(pixels)) {
				r.truncated_bytes = (uint32_t)count;
				fail(&r, ferror(input) ? "read-input" : "truncated-frame");
				break;
			}
			if (r.input_frames == MAX_FRAMES) {
				fail(&r, "input-frame-limit");
				break;
			}
			check_frame(pixels, &o, &r);
			r.input_frames++;
		}
		if (fclose(input) != 0)
			fail(&r, "close-input");
	}
	if (o.has_count && r.input_frames != o.require_count)
		fail(&r, "required-count");
	if (o.allow_damaged && (r.tail_next != o.frames ||
	    r.tail_exact_frames != o.tail_count))
		fail(&r, "incomplete-recovery-tail");
	if (report(&o, &r) != 0 || fflush(stdout) != 0) {
		fputs("check-frames: report-write\n", stderr);
		return EXIT_FAILURE;
	}
	return r.failure == NULL ? EXIT_SUCCESS : EXIT_FAILURE;
}
