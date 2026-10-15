/* SPDX-License-Identifier: MIT */
#ifndef EMBER_NCNN_APP_COMMON_H
#define EMBER_NCNN_APP_COMMON_H
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

struct configuration {
	const char *graph, *weights, *output, *input_name, *output_name;
	unsigned seed, count, repeats;
};
static const char expected_graph[] =
    "7767517\n6 6\n"
    "Input input 0 1 input 0=5 1=5 2=2\n"
    "Convolution conv 1 1 input conv 0=3 1=3 5=1 6=54\n"
    "ReLU relu 1 1 conv relu\n"
    "Pooling pool 1 1 relu pool 0=1 4=1\n"
    "InnerProduct dense 1 1 pool logits 0=3 1=1 2=9\n"
    "Softmax softmax 1 1 logits prob 0=0 1=1\n";

static int
parse_number(const char *text, unsigned low, unsigned high, unsigned *out)
{
	char *end;
	unsigned long n;
	const char *p;
	if (*text == '\0')
		return (-1);
	for (p = text; *p != '\0'; p++)
		if (*p < '0' || *p > '9')
			return (-1);
	errno = 0;
	n = strtoul(text, &end, 10);
	if (errno != 0 || *end != '\0' || n < low || n > high)
		return (-1);
	*out = (unsigned)n;
	return (0);
}

static int
parse_configuration(int argc, char **argv, struct configuration *c)
{
	struct stat a, b;
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		puts("Usage: ncnn-cpu-{c,cxx} MODEL.param MODEL.bin OUTPUT.csv SEED COUNT REPEAT [INPUT_BLOB OUTPUT_BLOB]");
		return (-1);
	}
	if ((argc != 7 && argc != 9) ||
	    parse_number(argv[4], 0, 100000, &c->seed) != 0 ||
	    parse_number(argv[5], 1, 64, &c->count) != 0 ||
	    parse_number(argv[6], 1, 1000, &c->repeats) != 0) {
		fprintf(stderr, "Expected paths, seed 0..100000, count 1..64, repeat 1..1000.\n");
		return (64);
	}
	c->graph = argv[1];
	c->weights = argv[2];
	c->output = argv[3];
	c->input_name = argc == 9 ? argv[7] : "input";
	c->output_name = argc == 9 ? argv[8] : "prob";
	if (*c->output == '\0' || strlen(c->input_name) > 64 ||
	    strlen(c->output_name) > 64) {
		fprintf(stderr, "Empty output path or excessive blob name.\n");
		return (64);
	}
	if (stat(c->output, &a) == 0 &&
	    ((stat(c->graph, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino) ||
	    (stat(c->weights, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino))) {
		fprintf(stderr, "Output must not replace a model input.\n");
		return (64);
	}
	return (0);
}

/* This wrapper accepts the fixed fixture, not arbitrary untrusted graphs. */
static int
validate_model(const struct configuration *c)
{
	FILE *f;
	unsigned char bytes[285];
	char graph[sizeof(expected_graph)];
	size_t n, i;
	int read_error, close_error;
	const uint16_t endian = 1;
	unsigned char endian_first;
	memcpy(&endian_first, &endian, 1);
	if (endian_first != 1 || sizeof(float) != 4) {
		fprintf(stderr, "This FP32 fixture requires a little-endian platform.\n");
		return (-1);
	}
	f = fopen(c->graph, "rb");
	if (f == NULL) {
		perror("Open graph");
		return (-1);
	}
	n = fread(graph, 1, sizeof(graph), f);
	read_error = ferror(f);
	close_error = fclose(f);
	if (read_error || close_error != 0 || n != sizeof(expected_graph) - 1 ||
	    memcmp(graph, expected_graph, sizeof(expected_graph) - 1) != 0) {
		fprintf(stderr, "Graph differs from the bounded six-layer fixture.\n");
		return (-1);
	}
	f = fopen(c->weights, "rb");
	if (f == NULL) {
		perror("Open weights");
		return (-1);
	}
	n = fread(bytes, 1, sizeof(bytes), f);
	read_error = ferror(f);
	close_error = fclose(f);
	if (read_error || close_error != 0 || n != 284) {
		fprintf(stderr, "Weights must contain exactly 284 bytes.\n");
		return (-1);
	}
	for (i = 0; i < 284; i += 4) {
		float value;
		if (i == 0 || i == 232) {
			if (bytes[i] != 0 || bytes[i+1] != 0 || bytes[i+2] != 0 || bytes[i+3] != 0) {
				fprintf(stderr, "Weights must use FP32 tags.\n");
				return (-1);
			}
			continue;
		}
		memcpy(&value, bytes + i, sizeof(value));
		if (!isfinite(value)) {
			fprintf(stderr, "Nonfinite model weight.\n");
			return (-1);
		}
	}
	return (0);
}

static void
make_input(unsigned seed, unsigned sample, unsigned channel, float *data)
{
	unsigned j;
	for (j = 0; j < 25; j++) {
		int raw = (int)(((seed + sample) * 17 + (channel * 25 + j) * 13) % 41);
		data[j] = (float)(raw - 20) / 16.0f;
	}
}

/* Write only after all inference calls pass. A failed run preserves the output. */
static int
write_results(const struct configuration *c, const float *values)
{
	char *temporary;
	FILE *f;
	int fd, status = 74;
	unsigned sample, repeat, k;
	size_t row = 0, length = strlen(c->output);
#ifdef __cplusplus
	temporary = static_cast<char *>(malloc(length + sizeof(".tmp.XXXXXX")));
#else
	temporary = malloc(length + sizeof(".tmp.XXXXXX"));
#endif
	if (temporary == NULL)
		return (70);
	memcpy(temporary, c->output, length);
	memcpy(temporary + length, ".tmp.XXXXXX", sizeof(".tmp.XXXXXX"));
	fd = mkstemp(temporary);
	if (fd < 0) {
		perror("Create output");
		free(temporary);
		return (74);
	}
	f = fdopen(fd, "w");
	if (f == NULL) {
		close(fd);
		unlink(temporary);
		free(temporary);
		return (74);
	}
	if (fprintf(f, "seed,sample,repeat,logit0,logit1,logit2,prob0,prob1,prob2\n") < 0)
		goto done;
	for (sample = 0; sample < c->count; sample++) {
		for (repeat = 0; repeat < c->repeats; repeat++, row++) {
			if (fprintf(f, "%u,%u,%u", c->seed, sample, repeat) < 0)
				goto done;
			for (k = 0; k < 6; k++)
				if (fprintf(f, ",%.9g", (double)values[row * 6 + k]) < 0)
					goto done;
			if (fputc('\n', f) == EOF)
				goto done;
		}
	}
	status = 0;
done:
	if (fclose(f) != 0)
		status = 74;
	if (status == 0 && rename(temporary, c->output) != 0) {
		perror("Commit output");
		status = 74;
	}
	if (status != 0)
		unlink(temporary);
	free(temporary);
	return (status);
}
#endif
