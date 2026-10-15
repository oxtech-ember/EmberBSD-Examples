/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 EmberBSD contributors (AI-assisted).
 * Bounded RTP receiver using the installed FFmpeg C API. UDP has no wire EOS.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>

struct run {
	int64_t start_us, last_packet_us, max_us, idle_us;
	int stop_reason, clock_error;
	unsigned int packets, frames, decode_errors;
	int64_t previous_pts;
	FILE *raw, *csv;
};

static int64_t
now_us(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return -1;
	return (int64_t)ts.tv_sec * INT64_C(1000000) + ts.tv_nsec / 1000;
}

static int
interrupted(void *opaque)
{
	struct run *run = opaque;
	int64_t now;

	now = now_us();
	if (now < 0) {
		run->clock_error = 1;
		return 1;
	}
	if (now - run->start_us >= run->max_us) {
		run->stop_reason = 2;
		return 1;
	}
	if (run->packets != 0 && now - run->last_packet_us >= run->idle_us) {
		run->stop_reason = 1;
		return 1;
	}
	return 0;
}

static int
number(const char *text, unsigned int maximum, unsigned int *value)
{
	char *end;
	unsigned long parsed;

	if (*text == '\0' || *text < '0' || *text > '9')
		return -1;
	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno != 0 || *end != '\0' || parsed == 0 || parsed > maximum)
		return -1;
	*value = (unsigned int)parsed;
	return 0;
}

static int
write_frame(struct run *run, const AVFrame *frame)
{
	int plane, row, width, height;
	int64_t pts;

	if (frame->width != 96 || frame->height != 64 ||
	    frame->format != AV_PIX_FMT_YUV420P || run->frames >= 1000) {
		fprintf(stderr, "Unexpected or excessive decoded frame\n");
		return -1;
	}
	pts = frame->best_effort_timestamp;
	if (pts == AV_NOPTS_VALUE || (run->frames != 0 && pts <= run->previous_pts)) {
		fprintf(stderr, "Missing or non-increasing decoded timestamp\n");
		return -1;
	}
	for (plane = 0; plane < 3; plane++) {
		width = plane == 0 ? frame->width : frame->width / 2;
		height = plane == 0 ? frame->height : frame->height / 2;
		if (frame->data[plane] == NULL || frame->linesize[plane] < width)
			return -1;
		for (row = 0; row < height; row++) {
			if (fwrite(frame->data[plane] + (size_t)row *
			    (size_t)frame->linesize[plane], 1, (size_t)width,
			    run->raw) != (size_t)width)
				return -1;
		}
	}
	if (fprintf(run->csv, "%u,%" PRId64 ",%" PRId64 ",%d,%d,%d\n",
	    run->frames, pts, frame->duration, frame->width, frame->height,
	    (frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) < 0)
		return -1;
	run->previous_pts = pts;
	run->frames++;
	return 0;
}

static int
drain(struct run *run, AVCodecContext *decoder, AVFrame *frame)
{
	int error;

	for (;;) {
		error = avcodec_receive_frame(decoder, frame);
		if (error == AVERROR(EAGAIN) || error == AVERROR_EOF)
			return 0;
		if (error < 0) {
			run->decode_errors++;
			return error == AVERROR_INVALIDDATA ? 0 : -1;
		}
		if (write_frame(run, frame) != 0) {
			av_frame_unref(frame);
			return -1;
		}
		av_frame_unref(frame);
	}
}

int
main(int argc, char **argv)
{
	struct run run = {0};
	AVFormatContext *format = NULL;
	AVCodecContext *decoder = NULL;
	const AVCodec *codec;
	AVDictionary *options = NULL;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL;
	FILE *ready = NULL, *report = NULL;
	unsigned int max_ms, idle_ms;
	int error, result = 1, opened = 0;
	char message[AV_ERROR_MAX_STRING_SIZE];

	if (argc != 8 || number(argv[5], 300000, &max_ms) != 0 ||
	    number(argv[6], 60000, &idle_ms) != 0 || idle_ms >= max_ms) {
		fprintf(stderr, "Usage: ffmpeg-h265-receive INPUT.sdp OUT.i420 "
		    "FRAMES.csv REPORT.json MAX_MS IDLE_MS READY\n");
		return 2;
	}
	run.max_us = (int64_t)max_ms * 1000;
	run.idle_us = (int64_t)idle_ms * 1000;
	/* Caller-selected outputs must be new files, including the report. */
	report = fopen(argv[4], "wx");
	if (report == NULL)
		return 1;
	run.start_us = now_us();
	if (run.start_us < 0) {
		run.clock_error = 1;
		goto done;
	}
	run.raw = fopen(argv[2], "wbx");
	run.csv = fopen(argv[3], "wx");
	if (run.raw == NULL || run.csv == NULL ||
	    fprintf(run.csv, "index,pts,duration,width,height,corrupted\n") < 0)
		goto done;
	format = avformat_alloc_context();
	packet = av_packet_alloc();
	frame = av_frame_alloc();
	if (format == NULL || packet == NULL || frame == NULL)
		goto done;
	format->interrupt_callback.callback = interrupted;
	format->interrupt_callback.opaque = &run;
	format->max_delay = 100000;
	if (av_dict_set(&options, "protocol_whitelist", "file,udp,rtp", 0) < 0 ||
	    av_dict_set(&options, "reorder_queue_size", "128", 0) < 0 ||
	    av_dict_set(&options, "buffer_size", "1048576", 0) < 0)
		goto done;
	error = avformat_open_input(&format, argv[1], NULL, &options);
	if (error < 0) {
		(void)av_strerror(error, message, sizeof(message));
		fprintf(stderr, "avformat_open_input: %s\n", message);
		goto done;
	}
	opened = 1;
	if (options != NULL || format->nb_streams != 1 ||
	    format->streams[0]->codecpar->codec_id != AV_CODEC_ID_HEVC ||
	    format->streams[0]->time_base.num != 1 ||
	    format->streams[0]->time_base.den != 90000)
		goto done;
	codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
	if (codec == NULL || (decoder = avcodec_alloc_context3(codec)) == NULL ||
	    avcodec_parameters_to_context(decoder,
	    format->streams[0]->codecpar) < 0)
		goto done;
	decoder->thread_count = 1;
	decoder->pkt_timebase = format->streams[0]->time_base;
	if (avcodec_open2(decoder, codec, NULL) < 0)
		goto done;
	ready = fopen(argv[7], "wx");
	if (ready == NULL)
		goto done;
	error = fprintf(ready, "FFmpeg RTP sockets open\n") < 0;
	if (fclose(ready) != 0)
		error = 1;
	ready = NULL;
	if (error != 0)
		goto done;
	for (;;) {
		error = av_read_frame(format, packet);
		if (error < 0) {
			if (run.stop_reason != 0 && !run.clock_error)
				break;
			(void)av_strerror(error, message, sizeof(message));
			fprintf(stderr, "av_read_frame: %s\n", message);
			goto done;
		}
		run.last_packet_us = now_us();
		if (run.last_packet_us < 0) {
			run.clock_error = 1;
			av_packet_unref(packet);
			goto done;
		}
		if (packet->stream_index != 0) {
			av_packet_unref(packet);
			goto done;
		}
		run.packets++;
		error = avcodec_send_packet(decoder, packet);
		av_packet_unref(packet);
		if (error == AVERROR_INVALIDDATA)
			run.decode_errors++;
		else if (error < 0)
			goto done;
		if (drain(&run, decoder, frame) != 0)
			goto done;
	}
	/* Local bounded end-of-input: this is not an RTP EOS acknowledgement. */
	if (avcodec_send_packet(decoder, NULL) < 0 ||
	    drain(&run, decoder, frame) != 0 || run.frames == 0 ||
	    run.stop_reason != 1)
		goto done;
	result = 0;
done:
	if (ready != NULL && fclose(ready) != 0)
		result = 1;
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&decoder);
	av_dict_free(&options);
	if (opened)
		avformat_close_input(&format);
	else
		avformat_free_context(format);
	if (run.raw != NULL && fclose(run.raw) != 0)
		result = 1;
	if (run.csv != NULL && fclose(run.csv) != 0)
		result = 1;
	if (fprintf(report, "{\"ok\":%s,\"avformat_version\":%u,"
	    "\"avcodec_version\":%u,\"packets\":%u,\"frames\":%u,"
	    "\"decode_errors\":%u,\"stop_reason\":%d,"
	    "\"clock_error\":%d,\"wire_eos\":false}\n",
	    result == 0 ? "true" : "false", avformat_version(), avcodec_version(),
	    run.packets, run.frames, run.decode_errors, run.stop_reason,
	    run.clock_error) < 0)
		result = 1;
	if (fclose(report) != 0)
		result = 1;
	return result;
}
