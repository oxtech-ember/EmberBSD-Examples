/* SPDX-License-Identifier: MIT */
#include "app-common.h"
#ifdef NCNN_PKGCONFIG_HEADER
#include <ncnn/c_api.h>
#else
#include <c_api.h>
#endif

int
main(int argc, char **argv)
{
	struct configuration c;
	ncnn_net_t net = NULL;
	ncnn_option_t opt = NULL;
	ncnn_mat_t input = NULL, logits = NULL, prob = NULL;
	ncnn_extractor_t ex = NULL;
	float *values = NULL;
	unsigned sample, repeat, channel, k;
	size_t row = 0;
	int parsed, status = 65;

	parsed = parse_configuration(argc, argv, &c);
	if (parsed != 0)
		return (parsed == -1 ? 0 : parsed);
	if (validate_model(&c) != 0)
		return (65);
	if (ncnn_version_number() != 20260526 || strcmp(ncnn_version(), "1.0.20260526") != 0) {
		fprintf(stderr, "This example requires ncnn 1.0.20260526.\n");
		return (65);
	}
	fprintf(stderr, "ncnn %s; C API; CPU FP32; one inference thread\n", ncnn_version());
	values = calloc((size_t)c.count * c.repeats * 6, sizeof(*values));
	if (values == NULL)
		return (70);
	net = ncnn_net_create();
	opt = ncnn_option_create();
	if (net == NULL || opt == NULL) {
		status = 70;
		goto done;
	}
	ncnn_option_set_num_threads(opt, 1);
	ncnn_option_set_use_vulkan_compute(opt, 0);
	ncnn_option_set_use_fp16_packed(opt, 0);
	ncnn_option_set_use_fp16_storage(opt, 0);
	ncnn_option_set_use_fp16_arithmetic(opt, 0);
	ncnn_option_set_use_bf16_packed(opt, 0);
	ncnn_option_set_use_bf16_storage(opt, 0);
	ncnn_option_set_use_int8_packed(opt, 0);
	ncnn_option_set_use_int8_storage(opt, 0);
	ncnn_option_set_use_int8_arithmetic(opt, 0);
	ncnn_option_set_use_packing_layout(opt, 0);
	ncnn_net_set_option(net, opt);
	if (ncnn_net_load_param(net, c.graph) != 0 || ncnn_net_load_model(net, c.weights) != 0) {
		fprintf(stderr, "ncnn cannot load the model.\n");
		goto done;
	}
	for (sample = 0; sample < c.count; sample++) {
		for (repeat = 0; repeat < c.repeats; repeat++, row++) {
			input = ncnn_mat_create_3d(5, 5, 2, NULL);
			if (input == NULL || ncnn_mat_get_data(input) == NULL) {
				status = 70;
				goto done;
			}
			for (channel = 0; channel < 2; channel++)
				make_input(c.seed, sample, channel,
				    (float *)ncnn_mat_get_channel_data(input, (int)channel));
			ex = ncnn_extractor_create(net);
			if (ex == NULL) {
				status = 70;
				goto done;
			}
			if (ncnn_extractor_input(ex, c.input_name, input) != 0 ||
			    ncnn_extractor_extract(ex, "logits", &logits) != 0)
				goto inference_error;
			if (ncnn_mat_get_dims(logits) != 1 || ncnn_mat_get_w(logits) != 3 ||
			    ncnn_mat_get_elemsize(logits) != sizeof(float) || ncnn_mat_get_elempack(logits) != 1)
				goto inference_error;
			for (k = 0; k < 3; k++)
				values[row * 6 + k] = ((const float *)ncnn_mat_get_data(logits))[k];
			if (ncnn_extractor_extract(ex, c.output_name, &prob) != 0)
				goto inference_error;
			if (ncnn_mat_get_dims(prob) != 1 || ncnn_mat_get_w(prob) != 3 ||
			    ncnn_mat_get_elemsize(prob) != sizeof(float) || ncnn_mat_get_elempack(prob) != 1)
				goto inference_error;
			for (k = 0; k < 3; k++)
				values[row * 6 + 3 + k] = ((const float *)ncnn_mat_get_data(prob))[k];
			for (k = 0; k < 6; k++)
				if (!isfinite(values[row * 6 + k]))
					goto inference_error;
			ncnn_mat_destroy(prob);
			prob = NULL;
			ncnn_mat_destroy(logits);
			logits = NULL;
			ncnn_extractor_destroy(ex);
			ex = NULL;
			ncnn_mat_destroy(input);
			input = NULL;
		}
	}
	status = write_results(&c, values);
	goto done;
inference_error:
	fprintf(stderr, "ncnn inference failed or returned an unexpected tensor.\n");
done:
	if (prob != NULL)
		ncnn_mat_destroy(prob);
	if (logits != NULL)
		ncnn_mat_destroy(logits);
	if (ex != NULL)
		ncnn_extractor_destroy(ex);
	if (input != NULL)
		ncnn_mat_destroy(input);
	if (net != NULL)
		ncnn_net_destroy(net);
	if (opt != NULL)
		ncnn_option_destroy(opt);
	free(values);
	return (status);
}
