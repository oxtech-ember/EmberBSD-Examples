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
#include "answer.h"
#include "ask-image.h"
#include "eqml_input.h"
#include "eqml_wsdisplay.h"
#include "eqml_wsmouse.h"
#include "guide.h"
#include "sparkworkshop.h"
#include "temp-path.h"
#include "view.h"

enum action {
	ACTION_NONE, ACTION_OPEN, ACTION_BACK, ACTION_NEXT, ACTION_DONE,
	ACTION_CAMERA, ACTION_CAPTURE, ACTION_RETAKE, ACTION_USE, ACTION_CANCEL,
	ACTION_ASK, ACTION_ANSWER_BACK, ACTION_ANSWER_PREVIOUS,
	ACTION_ANSWER_NEXT, ACTION_ANSWER_RETRY
};

enum ask_state { ASK_IDLE, ASK_RUNNING, ASK_READY, ASK_FAILED };

struct capture_result {
	int result;
	int error;
};

struct photo {
	uint16_t *pixels;
	struct eqml_bitmap bitmap;
	char *bmp_path;
	uint64_t generation;
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
	const char *temp_dir;
	char *bmp_path;
	int bmp_fd, reply_fd;
	pid_t child;
	unsigned int step;
	enum action action;
	struct capture_result reply;
	size_t reply_size;
	pid_t ask_child;
	int ask_fd;
	const char *agent_lua, *ask_script, *ask_config;
	char ask_output[WORKSHOP_ANSWER_MAX_BYTES + 3];
	size_t ask_size;
	uint64_t ask_generation, ask_current_generation;
	uint64_t photo_generation;
	uint64_t ask_photo_generation;
	unsigned int ask_step;
	enum ask_state ask_state;
	struct workshop_answer answer;
	size_t answer_page;
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
	else if (strcmp(name, "askPhotoRequested") == 0)
		app->action = ACTION_ASK;
	else if (strcmp(name, "answerBackRequested") == 0)
		app->action = ACTION_ANSWER_BACK;
	else if (strcmp(name, "answerPreviousRequested") == 0)
		app->action = ACTION_ANSWER_PREVIOUS;
	else if (strcmp(name, "answerNextRequested") == 0)
		app->action = ACTION_ANSWER_NEXT;
	else if (strcmp(name, "answerRetryRequested") == 0)
		app->action = ACTION_ANSWER_RETRY;
}

static int
show_step(struct app *app)
{
	const struct eqml_bitmap *bitmap = app->photos[app->step].pixels != NULL ?
	    &app->photos[app->step].bitmap : NULL;
	return workshop_view_step(app->ui, &app->guide, app->step, bitmap);
}

static int
show_answer(struct app *app)
{
	static const char waiting[WORKSHOP_ANSWER_LINES][WORKSHOP_ANSWER_LINE_BYTES] = {
		"Asking Spark about this", "photo.", "", "", "", "", ""
	};
	static const char failed[WORKSHOP_ANSWER_LINES][WORKSHOP_ANSWER_LINE_BYTES] = {
		"Spark could not answer.", "Check the connection and", "retry.", "", "", "", ""
	};
	const struct photo *photo;
	const char (*lines)[WORKSHOP_ANSWER_LINE_BYTES];
	const struct eqml_bitmap *bitmap;
	enum workshop_answer_state state;
	size_t page_count, page;

	if (app->step >= app->guide.step_count ||
	    app->photos[app->step].pixels == NULL) {
		errno = EINVAL;
		return -1;
	}
	photo = &app->photos[app->step];
	bitmap = &photo->bitmap;
	page = app->answer_page;
	if (app->ask_state == ASK_RUNNING) {
		state = WORKSHOP_ANSWER_WAITING;
		lines = waiting;
		page_count = 1;
	} else if (app->ask_state == ASK_READY) {
		state = WORKSHOP_ANSWER_READY;
		lines = app->answer.pages[page];
		page_count = app->answer.page_count;
	} else {
		state = WORKSHOP_ANSWER_ERROR;
		lines = failed;
		page_count = 1;
		page = 0;
	}
	return workshop_view_answer(app->ui, bitmap, state, page, page_count, lines);
}

static void
cancel_ask(struct app *app)
{
	if (++app->ask_generation == 0)
		++app->ask_generation;
	if (app->ask_child > 0) {
		(void)kill(app->ask_child, SIGTERM);
		while (waitpid(app->ask_child, NULL, 0) == -1 && errno == EINTR)
			continue;
		app->ask_child = -1;
	}
	if (app->ask_fd != -1) {
		(void)close(app->ask_fd);
		app->ask_fd = -1;
	}
	app->ask_size = 0;
	app->ask_state = ASK_IDLE;
}

static void
remove_pending(struct app *app)
{
	if (app->ui != NULL) {
		(void)eqml_ui_set_bitmap(app->ui, eqml_sparkworkshop_node_cameraPreview, NULL);
	}
	free(app->pending.pixels);
	if (app->pending.bmp_path != NULL) {
		(void)unlink(app->pending.bmp_path);
		free(app->pending.bmp_path);
	}
	memset(&app->pending, 0, sizeof(app->pending));
	if (app->bmp_fd != -1) {
		(void)close(app->bmp_fd);
		app->bmp_fd = -1;
	}
	if (app->bmp_path != NULL) {
		(void)unlink(app->bmp_path);
		free(app->bmp_path);
		app->bmp_path = NULL;
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
	char *template = NULL;
	int pipefd[2], flags;
	pid_t pid;

	if (app->child != -1) {
		return workshop_view_camera(app->ui, app->step,
		    "CAPTURE ALREADY RUNNING", NULL, 0);
	}
	if (workshop_temp_template(app->temp_dir, "spark-workshop-photo",
	    &template) == -1)
		return -1;
	app->bmp_fd = mkstemp(template);
	if (app->bmp_fd == -1) {
		int saved = errno;
		free(template);
		errno = saved;
		return -1;
	}
	app->bmp_path = template;
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
	fresh.bmp_path = app->bmp_path;
	app->bmp_path = NULL;
	app->pending = fresh;
	return 0;
}

static int
start_ask(struct app *app)
{
	char *png_name = NULL;
	char step_number[16];
	int png_fd = -1, pipefd[2] = { -1, -1 }, flags;
	pid_t pid;
	const struct workshop_step *step;
	const struct photo *photo;

	if (app->ask_child > 0)
		return show_answer(app);
	if (app->step >= app->guide.step_count ||
	    (photo = &app->photos[app->step])->pixels == NULL) {
		errno = ENOENT;
		return -1;
	}
	app->ask_state = ASK_FAILED;
	app->answer_page = 0;
	if (app->agent_lua == NULL || app->ask_script == NULL || app->ask_config == NULL) {
		errno = ENOENT;
		return show_answer(app);
	}
	step = &app->guide.steps[app->step];
	if (++app->ask_generation == 0)
		++app->ask_generation;
	app->ask_current_generation = app->ask_generation;
	app->ask_step = app->step;
	app->ask_photo_generation = photo->generation;
	app->ask_size = 0;
	memset(app->ask_output, 0, sizeof(app->ask_output));
	app->answer_page = 0;

	if (workshop_temp_template(app->temp_dir, "spark-workshop-ask",
	    &png_name) == -1)
		goto error;
	png_fd = mkstemp(png_name);
	if (png_fd == -1)
		goto error;
	if (unlink(png_name) == -1 || ftruncate(png_fd, 0) == -1)
		goto error;
	free(png_name);
	png_name = NULL;
	if (pipe(pipefd) == -1)
		goto error;
	pid = fork();
	if (pid == -1)
		goto error;
	if (pid == 0) {
		struct sigaction sa = { .sa_handler = SIG_DFL };
		int nullfd;
		char *worker_argv[9];
		(void)sigemptyset(&sa.sa_mask);
		(void)sigaction(SIGINT, &sa, NULL);
		(void)sigaction(SIGTERM, &sa, NULL);
		(void)sigaction(SIGHUP, &sa, NULL);
		(void)close(pipefd[0]);
		(void)close(app->display.fd);
		(void)close(app->mouse.fd);
		if (app->reply_fd != -1)
			(void)close(app->reply_fd);
		if (app->bmp_fd != -1)
			(void)close(app->bmp_fd);
		if (workshop_photo_png(photo->bmp_path, png_fd) == -1 ||
		    lseek(png_fd, 0, SEEK_SET) == -1 ||
		    dup2(png_fd, STDIN_FILENO) == -1 ||
		    dup2(pipefd[1], STDOUT_FILENO) == -1)
			_exit(126);
		nullfd = open("/dev/null", O_WRONLY);
		if (nullfd != -1) {
			(void)dup2(nullfd, STDERR_FILENO);
			if (nullfd > STDERR_FILENO)
				(void)close(nullfd);
		} else
			(void)close(STDERR_FILENO);
		if (png_fd > STDERR_FILENO)
			(void)close(png_fd);
		if (pipefd[1] > STDERR_FILENO)
			(void)close(pipefd[1]);
		snprintf(step_number, sizeof(step_number), "%u", app->step + 1);
		worker_argv[0] = (char *)app->agent_lua;
		worker_argv[1] = (char *)app->ask_script;
		worker_argv[2] = (char *)app->ask_config;
		worker_argv[3] = "-";
		worker_argv[4] = app->guide.title;
		worker_argv[5] = step_number;
		worker_argv[6] = (char *)step->title;
		worker_argv[7] = (char *)step->paragraph;
		worker_argv[8] = NULL;
		execv(app->agent_lua, worker_argv);
		_exit(127);
	}
	(void)close(pipefd[1]);
	pipefd[1] = -1;
	(void)close(png_fd);
	png_fd = -1;
	flags = fcntl(pipefd[0], F_GETFL);
	if (flags == -1 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) == -1) {
		int saved = errno;
		(void)kill(pid, SIGTERM);
		while (waitpid(pid, NULL, 0) == -1 && errno == EINTR)
			continue;
		(void)close(pipefd[0]);
		pipefd[0] = -1;
		errno = saved;
		goto error;
	}
	app->ask_child = pid;
	app->ask_fd = pipefd[0];
	app->ask_state = ASK_RUNNING;
	if (show_answer(app) == -1) {
		cancel_ask(app);
		return -1;
	}
	return 0;
error:
	{
		int saved = errno;
		if (pipefd[0] != -1)
			(void)close(pipefd[0]);
		if (pipefd[1] != -1)
			(void)close(pipefd[1]);
		if (png_fd != -1)
			(void)close(png_fd);
		if (png_name != NULL) {
			(void)unlink(png_name);
			free(png_name);
		}
		app->ask_state = ASK_FAILED;
		if (show_answer(app) == -1)
			return -1;
		errno = saved;
		return 0;
	}
}

static int
complete_ask(struct app *app)
{
	int status;
	ssize_t count;

	for (;;) {
		size_t capacity = sizeof(app->ask_output) - app->ask_size;
		if (capacity == 0) {
			cancel_ask(app);
			app->ask_state = ASK_FAILED;
			return show_answer(app);
		}
		count = read(app->ask_fd, app->ask_output + app->ask_size, capacity);
		if (count > 0) {
			app->ask_size += (size_t)count;
			if (app->ask_size > WORKSHOP_ANSWER_MAX_BYTES + 2) {
				cancel_ask(app);
				app->ask_state = ASK_FAILED;
				return show_answer(app);
			}
			continue;
		}
		if (count == -1 && errno == EINTR)
			continue;
		if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		if (count == -1) {
			cancel_ask(app);
			app->ask_state = ASK_FAILED;
			return show_answer(app);
		}
		break;
	}
	pid_t waited;
	do {
		waited = waitpid(app->ask_child, &status, 0);
	} while (waited == -1 && errno == EINTR);
	if (waited == -1) {
		int saved = errno;
		if (saved != ECHILD) {
			(void)kill(app->ask_child, SIGTERM);
			while (waitpid(app->ask_child, NULL, 0) == -1 && errno == EINTR)
				continue;
		}
		app->ask_child = -1;
		(void)close(app->ask_fd);
		app->ask_fd = -1;
		app->ask_state = ASK_FAILED;
		errno = saved;
		return show_answer(app);
	}
	app->ask_child = -1;
	(void)close(app->ask_fd);
	app->ask_fd = -1;
	if (app->ask_current_generation != app->ask_generation ||
	    app->ask_step != app->step ||
	    app->ask_photo_generation != app->photos[app->step].generation) {
		app->ask_state = ASK_IDLE;
		return 0;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
	    workshop_answer_parse_response(app->ask_output, app->ask_size,
	    &app->answer) == 0) {
		app->ask_state = ASK_READY;
		app->answer_page = 0;
		return show_answer(app);
	}
	app->ask_state = ASK_FAILED;
	return show_answer(app);
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
		cancel_ask(app);
		app->step = 0;
		return show_step(app);
	case ACTION_BACK:
		cancel_ask(app);
		if (app->step == 0)
			return workshop_view_home(app->ui, &app->guide);
		app->step--;
		return show_step(app);
	case ACTION_NEXT:
		cancel_ask(app);
		if (app->step + 1 < app->guide.step_count)
			app->step++;
		return show_step(app);
	case ACTION_DONE:
		cancel_ask(app);
		return workshop_view_home(app->ui, &app->guide);
	case ACTION_CAMERA:
		cancel_ask(app);
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
		cancel_ask(app);
		if (eqml_ui_set_bitmap(app->ui,
		    eqml_sparkworkshop_node_cameraPreview, NULL) == -1 ||
		    eqml_ui_set_bitmap(app->ui,
		    eqml_sparkworkshop_node_photoThumbnail, NULL) == -1 ||
		    eqml_ui_set_bitmap(app->ui,
		    eqml_sparkworkshop_node_answerPhoto, NULL) == -1)
			return -1;
		free(app->photos[app->step].pixels);
		if (app->photos[app->step].bmp_path != NULL) {
			(void)unlink(app->photos[app->step].bmp_path);
			free(app->photos[app->step].bmp_path);
		}
		app->photos[app->step] = app->pending;
		if (++app->photo_generation == 0)
			++app->photo_generation;
		app->photos[app->step].generation = app->photo_generation;
		memset(&app->pending, 0, sizeof(app->pending));
		if (app->bmp_fd != -1) {
			(void)close(app->bmp_fd);
			app->bmp_fd = -1;
		}
		return show_step(app);
	case ACTION_CANCEL:
		finish_child(app, 1);
		remove_pending(app);
		return show_step(app);
	case ACTION_ASK:
		return start_ask(app);
	case ACTION_ANSWER_BACK:
		cancel_ask(app);
		return show_step(app);
	case ACTION_ANSWER_PREVIOUS:
		if (app->ask_state == ASK_READY && app->answer_page != 0)
			app->answer_page--;
		return show_answer(app);
	case ACTION_ANSWER_NEXT:
		if (app->ask_state == ASK_READY &&
		    app->answer_page + 1 < app->answer.page_count)
			app->answer_page++;
		return show_answer(app);
	case ACTION_ANSWER_RETRY:
		return start_ask(app);
	}
	errno = EINVAL;
	return -1;
}

static int
run(struct app *app)
{
	struct eqml_wsmouse_event events[16];
	struct pollfd fds[3];
	while (!interrupted) {
		uint64_t now = monotonic_ms();
		int count = 1, capture_index = -1, ask_index = -1;
		int ready, timeout = eqml_ui_timeout(app->ui, now);
		fds[0] = (struct pollfd){ .fd = app->mouse.fd, .events = POLLIN };
		if (app->reply_fd != -1) {
			capture_index = count;
			fds[count++] = (struct pollfd){ .fd = app->reply_fd, .events = POLLIN };
		}
		if (app->ask_fd != -1) {
			ask_index = count;
			fds[count++] = (struct pollfd){ .fd = app->ask_fd, .events = POLLIN };
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
		if (capture_index != -1 && app->reply_fd == fds[capture_index].fd &&
		    (fds[capture_index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
			if (complete_capture(app) == -1)
				return -1;
		}
		if (ask_index != -1 && app->ask_fd == fds[ask_index].fd &&
		    (fds[ask_index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
			if (complete_ask(app) == -1)
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
	fprintf(stderr, "usage: %s ABSOLUTE_GUIDE.md [-d DISPLAY] [-m MOUSE] "
	    "[-c CAMERA] [-a AGENT_LUA -s ASK_SCRIPT -f CONFIG]\n",
	    program);
}

int
main(int argc, char **argv)
{
	struct app app = { .display.fd = -1, .mouse.fd = -1, .bmp_fd = -1,
	    .reply_fd = -1, .child = -1, .ask_fd = -1, .ask_child = -1,
	    .camera_device = "/dev/video0" };
	const char *display = "/dev/ttyE0", *mouse = "/dev/wsmouse0";
	const char *tmp_dir = getenv("TMPDIR");
	char *tmp_probe;
	struct sigaction sa = { .sa_handler = on_signal };
	int option, result = 1;
	app.temp_dir = tmp_dir == NULL || tmp_dir[0] == '\0' ? "/tmp" : tmp_dir;
	if (workshop_temp_template(app.temp_dir, "spark-workshop-probe",
	    &tmp_probe) == -1) {
		perror("TMPDIR");
		return 2;
	}
	free(tmp_probe);

	if (argc < 2 || argv[1][0] != '/') {
		usage(argv[0]);
		return 2;
	}
	if (workshop_guide_load(argv[1], &app.guide) == -1) {
		perror("read worktree guide");
		return 2;
	}
	optind = 2;
	while ((option = getopt(argc, argv, "d:m:c:a:s:f:")) != -1) {
		switch (option) {
		case 'd': display = optarg; break;
		case 'm': mouse = optarg; break;
		case 'c': app.camera_device = optarg; break;
		case 'a': app.agent_lua = optarg; break;
		case 's': app.ask_script = optarg; break;
		case 'f': app.ask_config = optarg; break;
		default: usage(argv[0]); return 2;
		}
	}
	if (optind != argc ||
	    (app.agent_lua != NULL && app.agent_lua[0] != '/') ||
	    (app.ask_script != NULL && app.ask_script[0] != '/') ||
	    (app.ask_config != NULL && app.ask_config[0] != '/')) {
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
	cancel_ask(&app);
	finish_child(&app, 1);
	remove_pending(&app);
	if (eqml_wsmouse_close(&app.mouse) == -1)
		result = 1;
	if (app.ui != NULL) {
		(void)eqml_ui_set_bitmap(app.ui,
		    eqml_sparkworkshop_node_photoThumbnail, NULL);
		(void)eqml_ui_set_bitmap(app.ui,
		    eqml_sparkworkshop_node_answerPhoto, NULL);
		eqml_ui_destroy(app.ui);
	}
	for (unsigned int i = 0; i < WORKSHOP_MAX_STEPS; i++) {
		if (app.photos[i].bmp_path != NULL) {
			(void)unlink(app.photos[i].bmp_path);
			free(app.photos[i].bmp_path);
		}
	}
	for (unsigned int i = 0; i < WORKSHOP_MAX_STEPS; i++)
		free(app.photos[i].pixels);
	if (eqml_wsdisplay_close(&app.display) == -1)
		result = 1;
	free(app.line);
	return result;
}
