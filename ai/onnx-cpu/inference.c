/* SPDX-License-Identifier: MIT */
#include <onnxruntime_c_api.h>
#include <stdint.h>
#include "app-common.h"

static void
report_ort_error(const OrtApi *api, OrtStatus *status)
{
	fprintf(stderr, "ONNX Runtime [%d]: %s\n", (int)api->GetErrorCode(status),
	    api->GetErrorMessage(status));
	api->ReleaseStatus(status);
}

#define ORT_CALL(expression) do { \
	OrtStatus *call_status = (expression); \
	if (call_status != NULL) { \
		report_ort_error(api, call_status); \
		goto done; \
	} \
} while (0)

int
main(int argc, char **argv)
{
	const OrtApi *api;
	struct configuration config;
	OrtEnv *env = NULL;
	OrtSessionOptions *options = NULL;
	OrtSession *session = NULL;
	OrtMemoryInfo *memory = NULL;
	OrtValue *input = NULL, *output = NULL;
	OrtTensorTypeAndShapeInfo *shape_info = NULL;
	char **providers = NULL;
	int provider_count = 0;
	float *input_data = NULL, *results = NULL;
	void *output_data = NULL;
	const char *input_names[1], *output_names[1];
	const OrtValue *inputs[1];
	int64_t shape[2], output_shape[2];
	ONNXTensorElementDataType element_type;
	size_t columns, run, row, dimensions, elements;
	int parsed, status = 65;

	parsed = parse_configuration(argc, argv, &config);
	if (parsed != 0)
		return (parsed == -1 ? 0 : parsed);
	api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
	if (api == NULL) {
		fprintf(stderr, "Installed ONNX Runtime does not support the header API.\n");
		return (65);
	}
	if (strcmp(OrtGetApiBase()->GetVersionString(), "1.31.0") != 0) {
		fprintf(stderr, "This example requires ONNX Runtime 1.31.0.\n");
		return (65);
	}
	ORT_CALL(api->GetAvailableProviders(&providers, &provider_count));
	if (provider_count != 1 || strcmp(providers[0], "CPUExecutionProvider") != 0) {
		fprintf(stderr, "This example requires the CPU-only Runtime profile.\n");
		goto done;
	}
	fprintf(stderr, "ONNX Runtime %s; provider %s; C API\n",
	    OrtGetApiBase()->GetVersionString(), providers[0]);
	{
		OrtStatus *release_status = api->ReleaseAvailableProviders(providers, provider_count);
		providers = NULL;
		if (release_status != NULL) {
			report_ort_error(api, release_status);
			goto done;
		}
	}
	ORT_CALL(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ember-onnx-c", &env));
	ORT_CALL(api->CreateSessionOptions(&options));
	ORT_CALL(api->SetIntraOpNumThreads(options, 1));
	ORT_CALL(api->SetInterOpNumThreads(options, 1));
	ORT_CALL(api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL));
	ORT_CALL(api->CreateSession(env, config.model, options, &session));
	ORT_CALL(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
	columns = config.wrong_shape ? 4 : 3;
	input_data = calloc(config.batch * columns, sizeof(*input_data));
	results = calloc(config.repeats * config.batch * 2, sizeof(*results));
	if (input_data == NULL || results == NULL) {
		fprintf(stderr, "Cannot allocate input or result buffer.\n");
		status = 70;
		goto done;
	}
	shape[0] = (int64_t)config.batch;
	shape[1] = (int64_t)columns;
	ORT_CALL(api->CreateTensorWithDataAsOrtValue(memory, input_data,
	    config.batch * columns * sizeof(*input_data), shape, 2,
	    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
	inputs[0] = input;
	input_names[0] = config.missing_input ? "absent_input" : "x";
	output_names[0] = config.missing_output ? "absent_output" : "y";
	for (run = 0; run < config.repeats; ++run) {
		for (row = 0; row < config.batch; ++row)
			make_input(row, run, input_data + row * columns);
		ORT_CALL(api->Run(session, NULL, input_names, inputs, 1,
		    output_names, 1, &output));
		ORT_CALL(api->GetTensorTypeAndShape(output, &shape_info));
		ORT_CALL(api->GetTensorElementType(shape_info, &element_type));
		ORT_CALL(api->GetDimensionsCount(shape_info, &dimensions));
		ORT_CALL(api->GetTensorShapeElementCount(shape_info, &elements));
		if (dimensions != 2 || elements != config.batch * 2 ||
		    element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
			fprintf(stderr, "Unexpected output tensor type or size.\n");
			goto done;
		}
		ORT_CALL(api->GetDimensions(shape_info, output_shape, 2));
		if (output_shape[0] != (int64_t)config.batch || output_shape[1] != 2) {
			fprintf(stderr, "Unexpected output tensor shape.\n");
			goto done;
		}
		ORT_CALL(api->GetTensorMutableData(output, &output_data));
		for (row = 0; row < config.batch * 2; ++row) {
			float value = ((const float *)output_data)[row];
			if (!isfinite(value)) {
				fprintf(stderr, "Nonfinite inference result.\n");
				goto done;
			}
			results[run * config.batch * 2 + row] = value;
		}
		api->ReleaseTensorTypeAndShapeInfo(shape_info);
		shape_info = NULL;
		api->ReleaseValue(output);
		output = NULL;
	}
	status = write_csv(&config, results);
done:
	if (shape_info != NULL)
		api->ReleaseTensorTypeAndShapeInfo(shape_info);
	if (output != NULL)
		api->ReleaseValue(output);
	if (input != NULL)
		api->ReleaseValue(input);
	if (memory != NULL)
		api->ReleaseMemoryInfo(memory);
	if (session != NULL)
		api->ReleaseSession(session);
	if (options != NULL)
		api->ReleaseSessionOptions(options);
	if (env != NULL)
		api->ReleaseEnv(env);
	if (providers != NULL) {
		OrtStatus *release_status = api->ReleaseAvailableProviders(providers, provider_count);
		if (release_status != NULL)
			report_ort_error(api, release_status);
	}
	free(input_data);
	free(results);
	return (status);
}
