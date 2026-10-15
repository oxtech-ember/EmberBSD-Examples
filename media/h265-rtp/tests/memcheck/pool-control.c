/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Negative modes deliberately inject one leak or one invalid write. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>

static void
work(gpointer data)
{
	g_atomic_int_inc((gint *)data);
}

static void
intentional_leak(void)
{
	void *p;

	p = malloc(16);
	if (p == NULL)
		exit(2);
	(void)memset(p, 0x35, 16);
	(void)fprintf(stderr, "intentional16B leak address=%p\n", p);
}

static void
intentional_invalid_write(void)
{
	unsigned char *p;
	volatile size_t index = 16;

	p = malloc(16);
	if (p == NULL)
		exit(2);
	p[index] = 0x35;
	free(p);
}

int
main(int argc, char **argv)
{
	static const char *factories[] = {
		"appsrc", "x265enc", "h265parse", "capsfilter",
		"rtph265pay", "multiudpsink"
	};
	GstElement *elements[G_N_ELEMENTS(factories)];
	GstTaskPool *pool;
	GError *error = NULL;
	gpointer task;
	guint i;
	gint completed = 0;

	if (!gst_init_check(NULL, NULL, &error)) {
		(void)fprintf(stderr, "gst_init: %s\n", error->message);
		g_clear_error(&error);
		return (2);
	}
	for (i = 0; i < G_N_ELEMENTS(factories); i++) {
		elements[i] = gst_element_factory_make(factories[i], NULL);
		if (elements[i] == NULL)
			return (2);
	}
	pool = gst_task_pool_new();
	gst_task_pool_prepare(pool, &error);
	if (error != NULL) {
		(void)fprintf(stderr, "prepare: %s\n", error->message);
		return (2);
	}
	task = gst_task_pool_push(pool, work, &completed, &error);
	if (error != NULL) {
		(void)fprintf(stderr, "push: %s\n", error->message);
		return (2);
	}
	if (task != NULL)
		gst_task_pool_join(pool, task);
	gst_task_pool_cleanup(pool);
	gst_object_unref(pool);
	for (i = 0; i < G_N_ELEMENTS(factories); i++)
		gst_object_unref(elements[i]);
	gst_deinit();
	if (g_atomic_int_get(&completed) != 1)
		return (2);
	if (argc == 2 && strcmp(argv[1], "--leak") == 0)
		intentional_leak();
	if (argc == 2 && strcmp(argv[1], "--invalid-write") == 0)
		intentional_invalid_write();
	return (0);
}
