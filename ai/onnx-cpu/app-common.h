/* SPDX-License-Identifier: MIT */
#ifndef EMBER_ONNX_APP_COMMON_H
#define EMBER_ONNX_APP_COMMON_H

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct configuration {
	const char *model;
	const char *output;
	size_t batch;
	size_t repeats;
	int wrong_shape;
	int missing_input;
	int missing_output;
};

static int
parse_count(const char *text, unsigned long maximum, size_t *result)
{
	char *end;
	unsigned long value;
	const char *p;

	if (*text == '\0')
		return (0);
	for (p = text; *p != '\0'; ++p)
		if (*p < '0' || *p > '9')
			return (0);
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno != 0 || *end != '\0' || value == 0 || value > maximum)
		return (0);
	*result = (size_t)value;
	return (1);
}

/* Return -1 for help, 0 for valid input, 64 for usage, or 74 for output. */
static int
parse_configuration(int argc, char **argv, struct configuration *config)
{
	memset(config, 0, sizeof(*config));
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		if (printf("Usage: %s MODEL BATCH REPEATS OUTPUT.csv "
		    "[--wrong-shape|--missing-input|--missing-output]\n", argv[0]) < 0 ||
		    fflush(stdout) != 0) {
			fprintf(stderr, "Cannot write help.\n");
			return (74);
		}
		return (-1);
	}
	if ((argc != 5 && argc != 6) || *argv[1] == '\0' ||
	    *argv[4] == '\0' || !parse_count(argv[2], 32, &config->batch) ||
	    !parse_count(argv[3], 1000, &config->repeats)) {
		fprintf(stderr, "Expected MODEL, BATCH (1..32), REPEATS (1..1000), "
		    "OUTPUT.csv, and at most one error switch.\n");
		return (64);
	}
	config->model = argv[1];
	config->output = argv[4];
	if (argc == 6) {
		if (strcmp(argv[5], "--wrong-shape") == 0)
			config->wrong_shape = 1;
		else if (strcmp(argv[5], "--missing-input") == 0)
			config->missing_input = 1;
		else if (strcmp(argv[5], "--missing-output") == 0)
			config->missing_output = 1;
		else {
			fprintf(stderr, "Unknown error switch.\n");
			return (64);
		}
	}
	return (0);
}

static void
make_input(size_t row, size_t run, float *input)
{
	input[0] = (float)row - 2.0f + (float)(run % 3) * 0.25f;
	input[1] = (float)((row * 3 + run) % 7) - 3.0f;
	input[2] = ((row + run) % 2 == 0) ? -1.5f : 0.5f;
}

/* Open output only after every inference succeeds. */
static int
write_csv(const struct configuration *config, const float *results)
{
	FILE *output;
	float input[3];
	size_t run, row, offset;
	int failed;

	output = fopen(config->output, "w");
	if (output == NULL) {
		perror("open output");
		return (74);
	}
	failed = fprintf(output, "run,row,x0,x1,x2,y0,y1\n") < 0;
	for (run = 0; run < config->repeats && !failed; ++run) {
		for (row = 0; row < config->batch && !failed; ++row) {
			make_input(row, run, input);
			offset = (run * config->batch + row) * 2;
			failed = fprintf(output, "%zu,%zu,%.9g,%.9g,%.9g,%.9g,%.9g\n",
			    run, row, (double)input[0], (double)input[1],
			    (double)input[2], (double)results[offset],
			    (double)results[offset + 1]) < 0;
		}
	}
	if (ferror(output))
		failed = 1;
	if (fclose(output) != 0)
		failed = 1;
	if (failed) {
		fprintf(stderr, "Cannot write complete output.\n");
		return (74);
	}
	return (0);
}

#endif
