/* SPDX-License-Identifier: BSD-2-Clause */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "camera.h"
#include "eqml_input.h"
#include "eqml_wsdisplay.h"
#include "eqml_wsmouse.h"
#include "guide.h"
#include "sparkworkshop.h"
#include "view.h"

enum action {
	ACTION_NONE, ACTION_OPEN, ACTION_BACK, ACTION_NEXT, ACTION_DONE,
	ACTION_CAMERA, ACTION_CAPTURE, ACTION_RETAKE, ACTION_USE, ACTION_CANCEL
};

struct capture_result {
	int result;
	int error;
};

struct photo {
	uint16_t *pixels;
	struct eqml_bitmap bitmap;
	char bmp_path[64];
};

struct app {
	struct eqml_ui *ui;
	struct eqml_wsdisplay display;
	struct eqml_wsmouse mouse;
	struct workshop_guide guide;
	struct photo photos[WORKSHOP_MAX_STEPS];
	struct photo pending;
	uint16_t *line;
	const char *camera_device;
	char bmp_path[64];
	int bmp_fd, reply_fd;
	pid_t child;
	unsigned int step;
	enum action action;
	struct capture_result reply;
	size_t reply_size;
};

static volatile sig_atomic_t interrupted;

static void
on_signal(int signal_number)
{
	(void)signal_number;
	interrupted = 1;
}

static uint64_t
monotonic_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		perror("CLOCK_MONOTONIC");
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

static int
present(void *context, const struct eqml_scene *scene,
	const struct eqml_region *region)
{
	struct app *app = context;
	return eqml_wsdisplay_present_region_at(&app->display, scene, NULL, 0, 0,
	    region, app->line, scene->width);
}

static void
qml_signal(void *context, const char *name)
{
	struct app *app = context;
	if (strcmp(name, "openWorkshopRequested") == 0)
		app->action = ACTION_OPEN;
	else if (strcmp(name, "previousStepRequested") == 0)
		app->action = ACTION_BACK;
	else if (strcmp(name, "nextStepRequested") == 0)
		app->action = ACTION_NEXT;
	else if (strcmp(name, "finishRequested") == 0)
		app->action = ACTION_DONE;
	else if (strcmp(name, "cameraRequested") == 0)
		app->action = ACTION_CAMERA;
	else if (strcmp(name, "cameraCaptureRequested") == 0)
		app->action = ACTION_CAPTURE;
	else if (strcmp(name, "cameraRetakeRequested") == 0)
		app->action = ACTION_RETAKE;
	else if (strcmp(name, "cameraUseRequested") == 0)
		app->action = ACTION_USE;
	else if (strcmp(name, "cameraCancelRequested") == 0)
		app->action = ACTION_CANCEL;
}

static int
show_step(struct app *app)
{
	const struct eqml_bitmap *bitmap = app->photos[app->step].pixels != NULL ?
	    &app->photos[app->step].bitmap : NULL;
	return workshop_view_step(app->ui, &app->guide, app->step, bitmap);
}

static void
remove_pending(struct app *app)
{
	if (app->ui != NULL) {
		(void)eqml_ui_set_bitmap(app->ui, eqml_sparkworkshop_node_cameraPreview, NULL);
	}
	free(app->pending.pixels);
	memset(&app->pending, 0, sizeof(app->pending));
	if (app->bmp_fd != -1) {
		(void)close(app->bmp_fd);
		app->bmp_fd = -1;
	}
	if (app->bmp_path[0] != '\0') {
		(void)unlink(app->bmp_path);
		app->bmp_path[0] = '\0';
	}
}

static void
finish_child(struct app *app, int cancel)
{
	if (app->child > 0) {
		if (cancel)
			(void)kill(app->child, SIGTERM);
		while (waitpid(app->child, NULL, 0) == -1 && errno == EINTR)
			continue;
		app->child = -1;
	}
	if (app->reply_fd != -1) {
		(void)close(app->reply_fd);
		app->reply_fd = -1;
	}
	app->reply_size = 0;
	memset(&app->reply, 0, sizeof(app->reply));
}

static int
start_capture(struct app *app)
{
	int pipefd[2], flags;
	pid_t pid;

	if (app->child != -1) {
		return workshop_view_camera(app->ui, app->step,
		    "CAPTURE ALREADY RUNNING", NULL, 0);
	}
	strcpy(app->bmp_path, "/tmp/spark-workshop-photo.XXXXXX");
	app->bmp_fd = mkstemp(app->bmp_path);
	if (app->bmp_fd == -1) {
		app->bmp_path[0] = '\0';
		return -1;
	}
	if (pipe(pipefd) == -1)
		goto fail_file;
	pid = fork();
	if (pid == -1) {
		(void)close(pipefd[0]);
		(void)close(pipefd[1]);
		goto fail_file;
	}
	if (pid == 0) {
		struct capture_result result = { .result = -1, .error = EIO };
		struct sigaction sa = { .sa_handler = SIG_DFL };
		ssize_t count;
		(void)sigemptyset(&sa.sa_mask);
		(void)sigaction(SIGINT, &sa, NULL);
		(void)sigaction(SIGTERM, &sa, NULL);
		(void)sigaction(SIGHUP, &sa, NULL);
		(void)close(pipefd[0]);
		(void)close(app->display.fd);
		(void)close(app->mouse.fd);
		if (workshop_camera_capture(app->camera_device, app->bmp_fd) == 0) {
			result.result = 0;
			result.error = 0;
		} else {
			result.error = errno;
		}
		do {
			count = write(pipefd[1], &result, sizeof(result));
		} while (count == -1 && errno == EINTR);
		(void)close(pipefd[1]);
		_exit(count == (ssize_t)sizeof(result) ? 0 : 1);
	}
	(void)close(pipefd[1]);
	flags = fcntl(pipefd[0], F_GETFL);
	if (flags == -1 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) == -1) {
		int saved = errno;
		(void)kill(pid, SIGTERM);
		while (waitpid(pid, NULL, 0) == -1 && errno == EINTR)
			continue;
		(void)close(pipefd[0]);
		errno = saved;
		goto fail_file;
	}
	app->child = pid;
	app->reply_fd = pipefd[0];
	app->reply_size = 0;
	memset(&app->reply, 0, sizeof(app->reply));
	return workshop_view_camera(app->ui, app->step, "CAPTURING ONE FRAME", NULL, 0);
fail_file:
	{
		int saved = errno;
		remove_pending(app);
		errno = saved;
		return -1;
	}
}

static int
accept_capture(struct app *app)
{
	uint16_t *pixels;
	struct photo fresh = {0};

	if (app->bmp_fd == -1 || app->pending.pixels != NULL) {
		errno = EINVAL;
		return -1;
	}
	pixels = malloc(WORKSHOP_THUMB_WIDTH * WORKSHOP_THUMB_HEIGHT * sizeof(*pixels));
	if (pixels == NULL)
		return -1;
	if (workshop_camera_thumbnail(app->bmp_fd, pixels,
	    WORKSHOP_THUMB_WIDTH * WORKSHOP_THUMB_HEIGHT) == -1) {
		free(pixels);
		return -1;
	}
	fresh.pixels = pixels;
	fresh.bitmap.width = WORKSHOP_THUMB_WIDTH;
	fresh.bitmap.height = WORKSHOP_THUMB_HEIGHT;
	fresh.bitmap.pixels = pixels;
	fresh.bitmap.pixel_count = WORKSHOP_THUMB_WIDTH * WORKSHOP_THUMB_HEIGHT;
	fresh.bitmap.alpha = NULL;
	fresh.bitmap.alpha_count = 0;
	memcpy(fresh.bmp_path, app->bmp_path, sizeof(fresh.bmp_path));
	app->pending = fresh;
	return 0;
}

static int
complete_capture(struct app *app)
{
	int status, error;
	ssize_t count;

	while (app->reply_size < sizeof(app->reply)) {
		count = read(app->reply_fd, (char *)&app->reply + app->reply_size,
		    sizeof(app->reply) - app->reply_size);
		if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		if (count == -1 && errno == EINTR)
			continue;
		if (count <= 0) {
			app->reply.result = -1;
			app->reply.error = EIO;
			break;
		}
		app->reply_size += (size_t)count;
	}
	status = app->reply.result;
	error = app->reply.error;
	finish_child(app, 0);
	if (status == 0 && accept_capture(app) == 0) {
		if (workshop_view_camera(app->ui, app->step,
		    "REVIEW FRAME / RETAKE OR USE", &app->pending.bitmap, 1) == -1)
			return -1;
		return 1;
	}
	remove_pending(app);
	if (workshop_view_camera(app->ui, app->step,
	    error == ETIMEDOUT ? "NO FRAME / RETRY OR BACK" : "CAPTURE FAILED / RETRY OR BACK",
	    NULL, 0) == -1)
		return -1;
	return 1;
}

static int
apply_action(struct app *app, enum action action)
{
	switch (action) {
	case ACTION_NONE:
		return 0;
	case ACTION_OPEN:
		app->step = 0;
		return show_step(app);
	case ACTION_BACK:
		if (app->step == 0)
			return workshop_view_home(app->ui, &app->guide);
		app->step--;
		return show_step(app);
	case ACTION_NEXT:
		if (app->step + 1 < app->guide.step_count)
			app->step++;
		return show_step(app);
	case ACTION_DONE:
		return workshop_view_home(app->ui, &app->guide);
	case ACTION_CAMERA:
		remove_pending(app);
		return workshop_view_camera(app->ui, app->step, "READY / TAKE A PHOTO", NULL, 0);
	case ACTION_CAPTURE:
		return start_capture(app);
	case ACTION_RETAKE:
		remove_pending(app);
		return start_capture(app);
	case ACTION_USE:
		if (app->pending.pixels == NULL) {
			errno = EINVAL;
			return -1;
		}
		if (eqml_ui_set_bitmap(app->ui,
		    eqml_sparkworkshop_node_cameraPreview, NULL) == -1 ||
		    eqml_ui_set_bitmap(app->ui,
		    eqml_sparkworkshop_node_photoThumbnail, NULL) == -1)
			return -1;
		free(app->photos[app->step].pixels);
		if (app->photos[app->step].bmp_path[0] != '\0')
			(void)unlink(app->photos[app->step].bmp_path);
		app->photos[app->step] = app->pending;
		memset(&app->pending, 0, sizeof(app->pending));
		app->pending.bmp_path[0] = '\0';
		if (app->bmp_fd != -1) {
			(void)close(app->bmp_fd);
			app->bmp_fd = -1;
		}
		app->bmp_path[0] = '\0';
		return show_step(app);
	case ACTION_CANCEL:
		finish_child(app, 1);
		remove_pending(app);
		return show_step(app);
	}
	errno = EINVAL;
	return -1;
}

static int
run(struct app *app)
{
	struct eqml_wsmouse_event events[16];
	struct pollfd fds[2];
	while (!interrupted) {
		uint64_t now = monotonic_ms();
		int count = 1, ready, timeout = eqml_ui_timeout(app->ui, now);
		fds[0] = (struct pollfd){ .fd = app->mouse.fd, .events = POLLIN };
		if (app->reply_fd != -1) {
			fds[count++] = (struct pollfd){ .fd = app->reply_fd, .events = POLLIN };
		}
		if (timeout < 0)
			timeout = -1;
		ready = poll(fds, (nfds_t)count, timeout);
		if (ready == -1 && errno == EINTR)
			continue;
		if (ready == -1)
			return -1;
		if (ready != 0 && (fds[0].revents & (POLLERR | POLLNVAL | POLLHUP)) != 0) {
			errno = EIO;
			return -1;
		}
		if (ready != 0 && (fds[0].revents & POLLIN) != 0) {
			int got = eqml_wsmouse_read(&app->mouse, events,
			    sizeof(events) / sizeof(events[0]));
			if (got == -1)
				return -1;
			for (int i = 0; i < got; i++) {
				if (eqml_ui_pointer(app->ui, events[i].phase, events[i].x,
				    events[i].y, events[i].button, events[i].modifiers,
				    events[i].now_ms) == -1)
					return -1;
				if (app->action != ACTION_NONE) {
					enum action action = app->action;
					app->action = ACTION_NONE;
					if (apply_action(app, action) == -1)
						return -1;
				}
			}
		}
		if (app->reply_fd != -1 && count == 2 &&
		    (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
			if (complete_capture(app) == -1)
				return -1;
		}
		if (eqml_ui_tick(app->ui, monotonic_ms()) == -1 ||
		    eqml_ui_present(app->ui, present, app) == -1)
			return -1;
	}
	return 0;
}

static void
usage(const char *program)
{
	fprintf(stderr, "usage: %s ABSOLUTE_GUIDE.md [-d DISPLAY] [-m MOUSE] [-c CAMERA]\n",
	    program);
}

int
main(int argc, char **argv)
{
	struct app app = { .display.fd = -1, .mouse.fd = -1, .bmp_fd = -1,
	    .reply_fd = -1, .child = -1, .camera_device = "/dev/video0" };
	const char *display = "/dev/ttyE0", *mouse = "/dev/wsmouse0";
	struct sigaction sa = { .sa_handler = on_signal };
	int option, result = 1;

	if (argc < 2 || argv[1][0] != '/') {
		usage(argv[0]);
		return 2;
	}
	if (workshop_guide_load(argv[1], &app.guide) == -1) {
		perror("read worktree guide");
		return 2;
	}
	optind = 2;
	while ((option = getopt(argc, argv, "d:m:c:")) != -1) {
		switch (option) {
		case 'd': display = optarg; break;
		case 'm': mouse = optarg; break;
		case 'c': app.camera_device = optarg; break;
		default: usage(argv[0]); return 2;
		}
	}
	if (optind != argc) {
		usage(argv[0]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (sigemptyset(&sa.sa_mask) == -1 || sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1 || sigaction(SIGHUP, &sa, NULL) == -1) {
		perror("signals");
		return 1;
	}
	eqml_wsmouse_init(&app.mouse);
	app.line = calloc(eqml_sparkworkshop.width, sizeof(*app.line));
	app.ui = eqml_ui_create(&eqml_sparkworkshop, NULL, NULL);
	if (app.line == NULL || app.ui == NULL ||
	    eqml_wsdisplay_open(&app.display, display, eqml_sparkworkshop.width,
	    eqml_sparkworkshop.height) == -1 || eqml_wsmouse_open(&app.mouse, mouse) == -1 ||
	    workshop_view_home(app.ui, &app.guide) == -1)
		perror("spark-workshop");
	else {
		eqml_ui_set_signal_handler(app.ui, qml_signal, &app);
		if (eqml_ui_present(app.ui, present, &app) == 0 && run(&app) == 0)
			result = 0;
		else
			perror("spark-workshop");
	}
	finish_child(&app, 1);
	remove_pending(&app);
	if (eqml_wsmouse_close(&app.mouse) == -1)
		result = 1;
	if (app.ui != NULL) {
		(void)eqml_ui_set_bitmap(app.ui,
		    eqml_sparkworkshop_node_photoThumbnail, NULL);
		eqml_ui_destroy(app.ui);
	}
	for (unsigned int i = 0; i < WORKSHOP_MAX_STEPS; i++)
		if (app.photos[i].bmp_path[0] != '\0')
			(void)unlink(app.photos[i].bmp_path);
	for (unsigned int i = 0; i < WORKSHOP_MAX_STEPS; i++)
		free(app.photos[i].pixels);
	if (eqml_wsdisplay_close(&app.display) == -1)
		result = 1;
	free(app.line);
	return result;
}
