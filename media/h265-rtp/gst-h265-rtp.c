/*
 * Copyright (c) 2026 EmberBSD contributors
 * SPDX-License-Identifier: MIT
 *
 * Exercise installed GStreamer plugins. This program does not implement RTP
 * packetization, depacketization, loss concealment, or an HEVC decoder.
 */

#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>

#define MAX_PLUGINS 16
#define MAX_FRAMES 1000U
#define MAX_FIXTURE_BYTES (64U * 1024U * 1024U)
#define RTP_SSRC 0x13572468U

struct plugin_record {
	char *factory;
	char *name;
	char *version;
	char *path;
	gboolean loaded;
};

struct run {
	GstElement *pipeline;
	GstElement *source;
	GstElement *sink;
	GstElement *jitter;
	GstBus *bus;
	FILE *raw;
	FILE *csv;
	const char *report_path;
	const char *ready_path;
	const char *mode;
	char *error;
	struct plugin_record plugins[MAX_PLUGINS];
	unsigned int plugin_count;
	unsigned int segment;
	unsigned int restarts;
	unsigned int frames;
	unsigned int segment_frames;
	unsigned int submitted;
	unsigned int bus_eos;
	unsigned int injected_eos;
	unsigned int warnings;
	unsigned int udp_timeouts;
	unsigned int corrupt_frames;
	unsigned int equal_pts;
	guint64 bytes;
	guint64 jitter_pushed;
	guint64 jitter_lost;
	guint64 jitter_late;
	guint64 jitter_duplicates;
	gint lost_events;
	gint keyframe_requests;
	GstClockTime previous_pts;
	gint64 start_us;
	gint64 first_frame_us;
	gint64 last_frame_us;
	gboolean have_pts;
	gboolean receive;
	gboolean success;
};

static int
fail(struct run *run, const char *message)
{
	if (run->error == NULL)
		run->error = g_strdup(message);
	return (-1);
}

static void
json_string(FILE *out, const char *value)
{
	const unsigned char *p;

	(void)fputc('"', out);
	for (p = (const unsigned char *)(value != NULL ? value : "");
	    *p != '\0'; p++) {
		switch (*p) {
		case '"':
			(void)fputs("\\\"", out);
			break;
		case '\\':
			(void)fputs("\\\\", out);
			break;
		case '\n':
			(void)fputs("\\n", out);
			break;
		case '\r':
			(void)fputs("\\r", out);
			break;
		case '\t':
			(void)fputs("\\t", out);
			break;
		default:
			if (*p < 0x20)
				(void)fprintf(out, "\\u%04x", (unsigned int)*p);
			else
				(void)fputc(*p, out);
		}
	}
	(void)fputc('"', out);
}

static int
write_report(struct run *run)
{
	FILE *out;
	guint major, minor, micro, nano;
	unsigned int i;
	int bad;

	out = fopen(run->report_path, "w");
	if (out == NULL) {
		perror(run->report_path);
		return (-1);
	}
	gst_version(&major, &minor, &micro, &nano);
	(void)fprintf(out, "{\n  \"schema\":1,\n  \"mode\":");
	json_string(out, run->mode);
	(void)fprintf(out, ",\n  \"success\":%s,\n  \"error\":",
	    run->success ? "true" : "false");
	json_string(out, run->error);
	(void)fprintf(out, ",\n  \"gstreamer\":\"%u.%u.%u.%u\",\n"
	    "  \"rtp_payload_type\":96,\n  \"rtp_ssrc_sender\":%u,\n"
	    "  \"frames\":%u,\n  \"last_segment_frames\":%u,\n"
	    "  \"submitted\":%u,\n"
	    "  \"bytes\":%" PRIu64 ",\n  \"segments\":%u,\n"
	    "  \"restarts\":%u,\n  \"bus_eos\":%u,\n"
	    "  \"injected_eos\":%u,\n  \"warnings\":%u,\n"
	    "  \"udp_timeouts\":%u,\n  \"corrupt_frames\":%u,\n"
	    "  \"equal_pts\":%u,\n"
	    "  \"lost_events\":%d,\n  \"keyframe_requests\":%d,\n"
	    "  \"jitter_pushed\":%" PRIu64 ",\n"
	    "  \"jitter_lost\":%" PRIu64 ",\n"
	    "  \"jitter_late\":%" PRIu64 ",\n"
	    "  \"jitter_duplicates\":%" PRIu64 ",\n"
	    "  \"elapsed_us\":%" PRId64 ",\n  \"plugins\":[\n",
	    major, minor, micro, nano, RTP_SSRC, run->frames, run->segment_frames,
	    run->submitted, (uint64_t)run->bytes, run->segment + 1,
	    run->restarts, run->bus_eos, run->injected_eos, run->warnings,
	    run->udp_timeouts, run->corrupt_frames, run->equal_pts,
	    g_atomic_int_get(&run->lost_events),
	    g_atomic_int_get(&run->keyframe_requests),
	    (uint64_t)run->jitter_pushed, (uint64_t)run->jitter_lost,
	    (uint64_t)run->jitter_late, (uint64_t)run->jitter_duplicates,
	    (int64_t)(g_get_monotonic_time() - run->start_us));
	for (i = 0; i < run->plugin_count; i++) {
		struct plugin_record *record;

		record = &run->plugins[i];
		(void)fputs("    {\"factory\":", out);
		json_string(out, record->factory);
		(void)fputs(",\"plugin\":", out);
		json_string(out, record->name);
		(void)fputs(",\"version\":", out);
		json_string(out, record->version);
		(void)fputs(",\"path\":", out);
		json_string(out, record->path);
		(void)fprintf(out, ",\"loaded\":%s}%s\n",
		    record->loaded ? "true" : "false",
		    i + 1 == run->plugin_count ? "" : ",");
	}
	(void)fputs("  ]\n}\n", out);
	bad = ferror(out);
	if (fclose(out) != 0)
		bad = 1;
	return (bad ? -1 : 0);
}

static GstElement *
make_element(struct run *run, const char *factory, const char *name)
{
	struct plugin_record *record;
	GstElementFactory *feature;
	GstElement *element;
	GstPlugin *plugin;
	unsigned int i;

	element = gst_element_factory_make(factory, name);
	if (element == NULL) {
		(void)fail(run, "required installed element is unavailable");
		(void)fprintf(stderr, "missing element: %s\n", factory);
		return (NULL);
	}
	if (!gst_bin_add(GST_BIN(run->pipeline), element)) {
		gst_object_unref(element);
		(void)fail(run, "cannot add element to pipeline");
		return (NULL);
	}
	for (i = 0; i < run->plugin_count; i++)
		if (strcmp(run->plugins[i].factory, factory) == 0)
			return (element);
	if (run->plugin_count == MAX_PLUGINS) {
		(void)fail(run, "plugin receipt capacity exceeded");
		return (NULL);
	}
	feature = gst_element_get_factory(element);
	plugin = gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(feature));
	if (plugin == NULL) {
		(void)fail(run, "factory has no loaded plugin provenance");
		return (NULL);
	}
	record = &run->plugins[run->plugin_count++];
	record->factory = g_strdup(factory);
	record->name = g_strdup(gst_plugin_get_name(plugin));
	record->version = g_strdup(gst_plugin_get_version(plugin));
	record->path = g_strdup(gst_plugin_get_filename(plugin));
	record->loaded = gst_plugin_is_loaded(plugin);
	gst_object_unref(plugin);
	if (!record->loaded || record->path == NULL) {
		(void)fail(run, "required plugin is not a loaded shared module");
		return (NULL);
	}
	return (element);
}

static int
consume_bus(struct run *run)
{
	GstMessage *message;
	const GstStructure *structure;
	GError *error;
	char *debug;
	int result;

	result = 0;
	while ((message = gst_bus_pop(run->bus)) != NULL) {
		switch (GST_MESSAGE_TYPE(message)) {
		case GST_MESSAGE_ERROR:
			error = NULL;
			debug = NULL;
			gst_message_parse_error(message, &error, &debug);
			(void)fprintf(stderr, "GStreamer error from %s: %s\n%s\n",
			    GST_OBJECT_NAME(message->src), error->message,
			    debug != NULL ? debug : "");
			(void)fail(run, error->message);
			g_clear_error(&error);
			g_free(debug);
			result = -1;
			break;
		case GST_MESSAGE_WARNING:
			error = NULL;
			debug = NULL;
			gst_message_parse_warning(message, &error, &debug);
			(void)fprintf(stderr, "GStreamer warning from %s: %s\n%s\n",
			    GST_OBJECT_NAME(message->src), error->message,
			    debug != NULL ? debug : "");
			run->warnings++;
			g_clear_error(&error);
			g_free(debug);
			break;
		case GST_MESSAGE_EOS:
			run->bus_eos++;
			break;
		case GST_MESSAGE_ELEMENT:
			structure = gst_message_get_structure(message);
			if (structure != NULL &&
			    gst_structure_has_name(structure, "GstUDPSrcTimeout"))
				run->udp_timeouts++;
			break;
		default:
			break;
		}
		gst_message_unref(message);
	}
	return (result);
}

static GstPadProbeReturn
loss_event(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	struct run *run = data;
	const GstStructure *structure;
	GstEvent *event;

	(void)pad;
	event = GST_PAD_PROBE_INFO_EVENT(info);
	structure = gst_event_get_structure(event);
	if (structure != NULL &&
	    gst_structure_has_name(structure, "GstRTPPacketLost"))
		g_atomic_int_inc(&run->lost_events);
	if (structure != NULL &&
	    gst_structure_has_name(structure, "GstForceKeyUnit"))
		g_atomic_int_inc(&run->keyframe_requests);
	return (GST_PAD_PROBE_OK);
}

static void
collect_jitter(struct run *run)
{
	GstStructure *stats;
	guint64 value;

	if (run->jitter == NULL)
		return;
	stats = NULL;
	g_object_get(run->jitter, "stats", &stats, NULL);
	if (stats == NULL)
		return;
	if (gst_structure_get_uint64(stats, "num-pushed", &value))
		run->jitter_pushed += value;
	if (gst_structure_get_uint64(stats, "num-lost", &value))
		run->jitter_lost += value;
	if (gst_structure_get_uint64(stats, "num-late", &value))
		run->jitter_late += value;
	if (gst_structure_get_uint64(stats, "num-duplicates", &value))
		run->jitter_duplicates += value;
	gst_structure_free(stats);
}

static void
stop_pipeline(struct run *run)
{
	if (run->pipeline == NULL)
		return;
	(void)gst_element_set_state(run->pipeline, GST_STATE_NULL);
	/* NULL stops the UDP and timer tasks before their final counters are read. */
	collect_jitter(run);
	if (run->bus != NULL)
		gst_object_unref(run->bus);
	gst_object_unref(run->pipeline);
	run->pipeline = NULL;
	run->bus = NULL;
	run->source = NULL;
	run->sink = NULL;
	run->jitter = NULL;
}

static int
start_pipeline(struct run *run)
{
	GstStateChangeReturn change;
	GstState state;

	run->bus = gst_element_get_bus(run->pipeline);
	change = gst_element_set_state(run->pipeline, GST_STATE_PLAYING);
	if (change == GST_STATE_CHANGE_FAILURE) {
		(void)consume_bus(run);
		return (fail(run, "pipeline rejected PLAYING"));
	}
	/* appsrc needs input before a non-live sender can finish preroll. */
	if (!run->receive)
		return (0);
	state = GST_STATE_NULL;
	change = gst_element_get_state(run->pipeline, &state, NULL,
	    2 * GST_SECOND);
	if (consume_bus(run) != 0)
		return (-1);
	if (change == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PLAYING)
		return (fail(run, "receiver did not enter PLAYING before ready"));
	return (0);
}

static int
make_receiver(struct run *run, unsigned int port)
{
	GstElement *depay, *au, *parser, *decoder, *convert, *raw_caps;
	GstCaps *caps;
	GstPad *pad;
	GObject *socket;
	FILE *ready;
	int bad;

	run->pipeline = gst_pipeline_new("h265-rtp-receiver");
	if (run->pipeline == NULL)
		return (fail(run, "cannot create receiver pipeline"));
	run->source = make_element(run, "udpsrc", "udp-in");
	run->jitter = make_element(run, "rtpjitterbuffer", "jitter");
	depay = make_element(run, "rtph265depay", "depay");
	au = make_element(run, "capsfilter", "access-units");
	parser = make_element(run, "h265parse", "parser");
	decoder = make_element(run, "avdec_h265", "decoder");
	convert = make_element(run, "videoconvert", "convert");
	raw_caps = make_element(run, "capsfilter", "i420");
	run->sink = make_element(run, "appsink", "frames");
	if (run->error != NULL)
		return (-1);
	caps = gst_caps_new_simple("application/x-rtp", "media", G_TYPE_STRING,
	    "video", "encoding-name", G_TYPE_STRING, "H265", "clock-rate",
	    G_TYPE_INT, 90000, "payload", G_TYPE_INT, 96, NULL);
	g_object_set(run->source, "address", "127.0.0.1", "port", (gint)port,
	    "caps", caps, "reuse", FALSE, "buffer-size", 1048576,
	    "timeout", (guint64)(250 * GST_MSECOND), NULL);
	gst_caps_unref(caps);
	g_object_set(run->jitter, "latency", 100U, "do-lost", TRUE,
	    "post-drop-messages", TRUE, "do-retransmission", FALSE, NULL);
	g_object_set(depay, "wait-for-keyframe", TRUE,
	    "request-keyframe", TRUE, NULL);
	caps = gst_caps_new_simple("video/x-h265", "stream-format",
	    G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", NULL);
	g_object_set(au, "caps", caps, NULL);
	gst_caps_unref(caps);
	g_object_set(decoder, "max-threads", 1, "output-corrupt", FALSE, NULL);
	caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
	    "I420", NULL);
	g_object_set(raw_caps, "caps", caps, NULL);
	gst_caps_unref(caps);
	g_object_set(run->sink, "sync", FALSE, "async", FALSE,
	    "emit-signals", FALSE, "max-buffers", 8U,
	    "leaky-type", GST_APP_LEAKY_TYPE_NONE,
	    "wait-on-eos", FALSE, "enable-last-sample", FALSE, NULL);
	if (!gst_element_link_many(run->source, run->jitter, depay, au,
	    parser, decoder, convert, raw_caps, run->sink, NULL))
		return (fail(run, "cannot link explicit receiver elements"));
	pad = gst_element_get_static_pad(run->jitter, "src");
	(void)gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
	    loss_event, run, NULL);
	gst_object_unref(pad);
	pad = gst_element_get_static_pad(depay, "sink");
	(void)gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
	    loss_event, run, NULL);
	gst_object_unref(pad);
	if (start_pipeline(run) != 0)
		return (-1);
	socket = NULL;
	g_object_get(run->source, "used-socket", &socket, NULL);
	if (socket == NULL)
		return (fail(run, "receiver has no bound UDP socket"));
	g_object_unref(socket);
	ready = fopen(run->ready_path, run->segment == 0 ? "w" : "a");
	if (ready == NULL)
		return (fail(run, "cannot open receiver readiness file"));
	bad = fprintf(ready, "{\"segment\":%u,\"port\":%u,\"playing\":true}\n",
	    run->segment, port) < 0;
	if (fclose(ready) != 0)
		bad = 1;
	if (bad)
		return (fail(run, "cannot write receiver readiness file"));
	run->have_pts = FALSE;
	run->segment_frames = 0;
	run->last_frame_us = g_get_monotonic_time();
	return (0);
}

static int
write_sample(struct run *run, GstSample *sample)
{
	GstVideoInfo info;
	GstVideoFrame frame;
	GstBuffer *buffer;
	GChecksum *checksum;
	guint width, height, plane;
	gint decoded_width, decoded_height;
	guint64 size;
	GstClockTime pts, duration;
	int result;

	buffer = gst_sample_get_buffer(sample);
	if (buffer == NULL || !gst_video_info_from_caps(&info,
	    gst_sample_get_caps(sample)) ||
	    GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_I420)
		return (fail(run, "decoded sample is not an I420 video frame"));
	decoded_width = GST_VIDEO_INFO_WIDTH(&info);
	decoded_height = GST_VIDEO_INFO_HEIGHT(&info);
	if (decoded_width <= 0 || decoded_height <= 0 ||
	    decoded_width > 4096 || decoded_height > 4096 ||
	    decoded_width % 2 != 0 || decoded_height % 2 != 0 ||
	    run->frames == MAX_FRAMES)
		return (fail(run, "decoded frame exceeds the bounded fixture contract"));
	width = (guint)decoded_width;
	height = (guint)decoded_height;
	size = (guint64)width * height * 3 / 2;
	if (run->bytes > MAX_FIXTURE_BYTES - size)
		return (fail(run, "decoded bytes exceed the bounded fixture contract"));
	pts = GST_BUFFER_PTS(buffer);
	duration = GST_BUFFER_DURATION(buffer);
	if (!GST_CLOCK_TIME_IS_VALID(pts) ||
	    (run->have_pts && pts < run->previous_pts))
		return (fail(run, "decoded PTS is absent or goes backward in segment"));
	if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ))
		return (fail(run, "cannot map decoded video frame"));
	checksum = g_checksum_new(G_CHECKSUM_SHA256);
	result = 0;
	for (plane = 0; plane < 3; plane++) {
		const guint8 *row;
		guint y, row_width, row_count;
		gint stride;

		row_width = plane == 0 ? width : width / 2;
		row_count = plane == 0 ? height : height / 2;
		stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, plane);
		if ((gint64)stride < (gint64)row_width) {
			result = fail(run, "decoded I420 plane has an unsupported stride");
			break;
		}
		row = GST_VIDEO_FRAME_PLANE_DATA(&frame, plane);
		for (y = 0; y < row_count; y++) {
			if (fwrite(row, 1, row_width, run->raw) != row_width) {
				result = fail(run, "cannot write decoded pixel bytes");
				break;
			}
			g_checksum_update(checksum, row, row_width);
			row += stride;
		}
		if (result != 0)
			break;
	}
	if (result == 0 && fprintf(run->csv,
	    "%u,%u,%" PRIu64 ",%" PRIu64 ",%u,%u,%" PRIu64 ",%s,%u,%u\n",
	    run->segment, run->frames, (uint64_t)pts,
	    (uint64_t)duration, width, height, (uint64_t)size,
	    g_checksum_get_string(checksum),
	    GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT) ? 1U : 0U,
	    GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED) ? 1U : 0U) < 0)
		result = fail(run, "cannot write decoded frame record");
	if (result == 0) {
		run->frames++;
		run->segment_frames++;
		run->bytes += size;
		/* Slave-mode jitter correction can clamp PTS to the previous PTS. */
		if (run->have_pts && pts == run->previous_pts)
			run->equal_pts++;
		run->previous_pts = pts;
		run->have_pts = TRUE;
		run->last_frame_us = g_get_monotonic_time();
		if (run->first_frame_us == 0)
			run->first_frame_us = run->last_frame_us;
		if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED))
			run->corrupt_frames++;
	}
	g_checksum_free(checksum);
	gst_video_frame_unmap(&frame);
	return (result);
}

static int
receive_stream(struct run *run, unsigned int port, unsigned int max_ms,
    unsigned int idle_ms, unsigned int restart_ms, unsigned int pause_ms)
{
	GstSample *sample;
	gint64 restart_until, eos_deadline;
	int result;

	if (make_receiver(run, port) != 0)
		return (-1);
	restart_until = 0;
	eos_deadline = 0;
	for (;;) {
		gint64 now;

		now = g_get_monotonic_time();
		if (now - run->start_us > (gint64)max_ms * 1000)
			return (fail(run, "receiver exceeded its absolute deadline"));
		if (run->pipeline == NULL) {
			if (now < restart_until) {
				g_usleep(10000);
				continue;
			}
			run->segment++;
			if (make_receiver(run, port) != 0)
				return (-1);
			continue;
		}
		if (consume_bus(run) != 0)
			return (-1);
		if (restart_ms != 0 && run->restarts == 0 &&
		    run->first_frame_us != 0 && run->bus_eos == 0 &&
		    eos_deadline == 0 &&
		    now - run->first_frame_us >= (gint64)restart_ms * 1000) {
			/* Measure the fault after decoded data, not plugin startup. */
			/* Check before pulling so a full queue cannot starve the timer. */
			stop_pipeline(run);
			run->restarts++;
			restart_until = g_get_monotonic_time() +
			    (gint64)pause_ms * 1000;
			continue;
		}
		sample = gst_app_sink_try_pull_sample(GST_APP_SINK(run->sink),
		    10 * GST_MSECOND);
		if (sample != NULL) {
			result = write_sample(run, sample);
			gst_sample_unref(sample);
			if (result != 0)
				return (-1);
			continue;
		}
		if (run->bus_eos != 0) {
			if (run->segment_frames == 0)
				return (fail(run, "receiver segment reached EOS without a decoded frame"));
			return (0);
		}
		if (eos_deadline != 0) {
			if (now >= eos_deadline)
				return (fail(run, "receiver did not drain after local EOS injection"));
			continue;
		}
		if (run->frames != 0 &&
		    now - run->last_frame_us >= (gint64)idle_ms * 1000) {
			/* RTP/UDP has no EOS. This is an explicit local idle policy. */
			if (!gst_element_send_event(run->source, gst_event_new_eos()))
				return (fail(run, "receiver rejected locally injected EOS"));
			run->injected_eos++;
			eos_deadline = now + 2 * G_USEC_PER_SEC;
		}
	}
}

static int
send_stream(struct run *run, const char *input_path, unsigned int width,
    unsigned int height, unsigned int fps, const char *host,
    unsigned int port, unsigned int max_ms)
{
	struct stat statbuf;
	GstElement *encoder, *parser, *au, *pay;
	GstCaps *caps;
	GstBuffer *buffer;
	GstMapInfo map;
	GstFlowReturn flow;
	FILE *input;
	char *clients;
	guint64 frame_size, frame_count, index;
	int result;

	input = fopen(input_path, "rb");
	if (input == NULL)
		return (fail(run, "cannot open raw I420 input"));
	frame_size = (guint64)width * height * 3 / 2;
	result = -1;
	if (fstat(fileno(input), &statbuf) != 0 || !S_ISREG(statbuf.st_mode) ||
	    statbuf.st_size <= 0 || (guint64)statbuf.st_size > MAX_FIXTURE_BYTES ||
	    (guint64)statbuf.st_size % frame_size != 0) {
		(void)fail(run, "input must be a bounded whole-frame regular I420 file");
		goto done;
	}
	frame_count = (guint64)statbuf.st_size / frame_size;
	if (frame_count > MAX_FRAMES) {
		(void)fail(run, "raw input has too many frames");
		goto done;
	}
	run->pipeline = gst_pipeline_new("h265-rtp-sender");
	if (run->pipeline == NULL) {
		(void)fail(run, "cannot create sender pipeline");
		goto done;
	}
	run->source = make_element(run, "appsrc", "raw-input");
	encoder = make_element(run, "x265enc", "encoder");
	parser = make_element(run, "h265parse", "parser");
	au = make_element(run, "capsfilter", "access-units");
	pay = make_element(run, "rtph265pay", "pay");
	/* multiudpsink has no implicit localhost destination or DNS lookup. */
	run->sink = make_element(run, "multiudpsink", "udp-out");
	if (run->error != NULL)
		goto done;
	caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
	    "I420", "width", G_TYPE_INT, (gint)width, "height", G_TYPE_INT,
	    (gint)height, "framerate", GST_TYPE_FRACTION, (gint)fps, 1, NULL);
	g_object_set(run->source, "caps", caps, "format", GST_FORMAT_TIME,
	    "is-live", FALSE, "block", FALSE, "max-bytes", (guint64)0,
	    "size", (gint64)statbuf.st_size, NULL);
	gst_caps_unref(caps);
	gst_util_set_object_arg(G_OBJECT(encoder), "speed-preset", "ultrafast");
	gst_util_set_object_arg(G_OBJECT(encoder), "tune", "zerolatency");
	g_object_set(encoder, "key-int-max", 4, "option-string",
	    "lossless=1:min-keyint=4:scenecut=0:repeat-headers=1:aud=1:"
	    "bframes=0:open-gop=0:pools=none:frame-threads=1:wpp=0", NULL);
	caps = gst_caps_new_simple("video/x-h265", "stream-format",
	    G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", NULL);
	g_object_set(au, "caps", caps, NULL);
	gst_caps_unref(caps);
	g_object_set(pay, "pt", 96U, "ssrc", RTP_SSRC, "seqnum-offset", 0,
	    "timestamp-offset", 0U, "mtu", 512U, "config-interval", -1, NULL);
	gst_util_set_object_arg(G_OBJECT(pay), "aggregate-mode", "none");
	clients = g_strdup_printf("%s:%u", host, port);
	g_object_set(run->sink, "clients", clients,
	    "sync", TRUE, "async", FALSE, "enable-last-sample", FALSE, NULL);
	g_free(clients);
	if (!gst_element_link_many(run->source, encoder, parser, au, pay,
	    run->sink, NULL)) {
		(void)fail(run, "cannot link explicit sender elements");
		goto done;
	}
	if (start_pipeline(run) != 0)
		goto done;
	for (index = 0; index < frame_count; index++) {
		if (consume_bus(run) != 0)
			goto done;
		if (g_get_monotonic_time() - run->start_us > (gint64)max_ms * 1000) {
			(void)fail(run, "sender exceeded its input deadline");
			goto done;
		}
		buffer = gst_buffer_new_allocate(NULL, (gsize)frame_size, NULL);
		if (buffer == NULL) {
			(void)fail(run, "cannot allocate raw input buffer");
			goto done;
		}
		if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
			gst_buffer_unref(buffer);
			(void)fail(run, "cannot map raw input buffer");
			goto done;
		}
		if (fread(map.data, 1, (size_t)frame_size, input) != frame_size) {
			gst_buffer_unmap(buffer, &map);
			gst_buffer_unref(buffer);
			(void)fail(run, "cannot read complete raw input frame");
			goto done;
		}
		gst_buffer_unmap(buffer, &map);
		GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(index, GST_SECOND, fps);
		GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
		GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(index + 1,
		    GST_SECOND, fps) - GST_BUFFER_PTS(buffer);
		GST_BUFFER_OFFSET(buffer) = index;
		GST_BUFFER_OFFSET_END(buffer) = index + 1;
		flow = gst_app_src_push_buffer(GST_APP_SRC(run->source), buffer);
		if (flow != GST_FLOW_OK) {
			(void)fail(run, "appsrc rejected a timestamped raw frame");
			goto done;
		}
		run->submitted++;
		run->bytes += frame_size;
	}
	if (gst_app_src_end_of_stream(GST_APP_SRC(run->source)) != GST_FLOW_OK) {
		(void)fail(run, "appsrc rejected finite input EOS");
		goto done;
	}
	for (;;) {
		if (consume_bus(run) != 0)
			goto done;
		if (run->bus_eos != 0)
			break;
		if (g_get_monotonic_time() - run->start_us > (gint64)max_ms * 1000) {
			(void)fail(run, "sender exceeded its EOS deadline");
			goto done;
		}
		g_usleep(10000);
	}
	result = 0;
done:
	if (fclose(input) != 0)
		result = fail(run, "cannot close raw input");
	return (result);
}

static int
number(const char *text, unsigned int low, unsigned int high,
    unsigned int *value)
{
	char *end;
	unsigned long parsed;

	if (*text < '0' || *text > '9')
		return (-1);
	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno != 0 || *end != '\0' || parsed < low || parsed > high)
		return (-1);
	*value = (unsigned int)parsed;
	return (0);
}

static void
usage(const char *program)
{
	(void)fprintf(stderr,
	    "usage: %s send RAW_I420 WIDTH HEIGHT FPS IPV4 PORT REPORT MAX_MS\n"
	    "       %s receive PORT RAW_OUT CSV REPORT MAX_MS IDLE_MS READY_FILE"
	    " [RESTART_AFTER_MS RESTART_PAUSE_MS]\n", program, program);
}

int
main(int argc, char **argv)
{
	struct run run;
	GError *error;
	unsigned int width, height, fps, port, max_ms, idle_ms;
	unsigned int restart_ms, pause_ms, i;
	int result;

	(void)memset(&run, 0, sizeof(run));
	restart_ms = pause_ms = idle_ms = 0;
	width = height = fps = port = max_ms = 0;
	if (argc == 10 && strcmp(argv[1], "send") == 0) {
		struct in_addr address;

		/* Width / 2 is a multiple of the default I420 stride alignment. */
		if (number(argv[3], 16, 4096, &width) != 0 || width % 8 != 0 ||
		    number(argv[4], 16, 4096, &height) != 0 || height % 2 != 0 ||
		    number(argv[5], 1, 120, &fps) != 0 ||
		    inet_pton(AF_INET, argv[6], &address) != 1 ||
		    number(argv[7], 1024, 65535, &port) != 0 ||
		    number(argv[9], 1, 300000, &max_ms) != 0 ||
		    strcmp(argv[2], argv[8]) == 0) {
			usage(argv[0]);
			return (64);
		}
		run.mode = "send";
		run.report_path = argv[8];
	} else if ((argc == 9 || argc == 11) &&
	    strcmp(argv[1], "receive") == 0) {
		if (number(argv[2], 1024, 65535, &port) != 0 ||
		    number(argv[6], 1, 300000, &max_ms) != 0 ||
		    number(argv[7], 1, 300000, &idle_ms) != 0 ||
		    (argc == 11 &&
		    (number(argv[9], 1, 300000, &restart_ms) != 0 ||
		    number(argv[10], 1, 300000, &pause_ms) != 0))) {
			usage(argv[0]);
			return (64);
		}
		for (i = 3; i <= 5; i++) {
			unsigned int j;
			for (j = i + 1; j <= 5; j++)
				if (strcmp(argv[i], argv[j]) == 0) {
					usage(argv[0]);
					return (64);
				}
			if (strcmp(argv[i], argv[8]) == 0) {
				usage(argv[0]);
				return (64);
			}
		}
		run.mode = "receive";
		run.receive = TRUE;
		run.report_path = argv[5];
		run.ready_path = argv[8];
	} else {
		usage(argv[0]);
		return (64);
	}
	/* Parse only this program's CLI. Do not let gst_init remove arguments. */
	error = NULL;
	if (!gst_init_check(NULL, NULL, &error)) {
		(void)fprintf(stderr, "gst_init: %s\n", error->message);
		g_clear_error(&error);
		return (1);
	}
	run.start_us = g_get_monotonic_time();
	result = -1;
	if (run.receive) {
		run.raw = fopen(argv[3], "wb");
		run.csv = fopen(argv[4], "w");
		if (run.raw == NULL || run.csv == NULL) {
			(void)fail(&run, "cannot open receiver output files");
		} else if (fprintf(run.csv, "segment,index,pts_ns,duration_ns,"
		    "width,height,bytes,sha256,discont,corrupted\n") < 0) {
			(void)fail(&run, "cannot write receiver CSV header");
		} else {
			result = receive_stream(&run, port, max_ms, idle_ms,
			    restart_ms, pause_ms);
		}
	} else {
		result = send_stream(&run, argv[2], width, height, fps, argv[6],
		    port, max_ms);
	}
	stop_pipeline(&run);
	if (run.raw != NULL && fclose(run.raw) != 0)
		result = fail(&run, "cannot close decoded pixel output");
	if (run.csv != NULL && fclose(run.csv) != 0)
		result = fail(&run, "cannot close decoded CSV output");
	run.success = result == 0;
	if (write_report(&run) != 0)
		result = -1;
	if (run.error != NULL)
		(void)fprintf(stderr, "%s\n", run.error);
	g_free(run.error);
	for (i = 0; i < run.plugin_count; i++) {
		g_free(run.plugins[i].factory);
		g_free(run.plugins[i].name);
		g_free(run.plugins[i].version);
		g_free(run.plugins[i].path);
	}
	gst_deinit();
	return (result == 0 ? 0 : 1);
}
