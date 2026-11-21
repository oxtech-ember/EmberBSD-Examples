/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/videoio.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "camera.h"
#include "camera-bmp.h"

enum { NBUFS = 2, TIMEOUT_MS = 5000 };
struct mapping {
	void *data;
	size_t length;
};

static int
monotonic_ms(int64_t *ms)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) == -1)
		return -1;
	*ms = (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
	return 0;
}

static int
capture_buffer(int fd, struct v4l2_buffer *buf)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int64_t now, deadline;

	if (monotonic_ms(&now) == -1)
		return -1;
	deadline = now + TIMEOUT_MS;
	for (;;) {
		if (monotonic_ms(&now) == -1)
			return -1;
		if (now >= deadline) {
			errno = ETIMEDOUT;
			return -1;
		}
		int ready = poll(&pfd, 1, (int)(deadline - now));
		if (ready == -1 && errno == EINTR)
			continue;
		if (ready == -1)
			return -1;
		if (ready == 0) {
			errno = ETIMEDOUT;
			return -1;
		}
		if ((pfd.revents & (POLLERR | POLLNVAL | POLLHUP)) != 0) {
			errno = EIO;
			return -1;
		}
		if (ioctl(fd, VIDIOC_DQBUF, buf) == 0)
			return 0;
		if (errno != EAGAIN && errno != EINTR)
			return -1;
	}
}

int
workshop_camera_capture(const char *device, int bmp_fd)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	struct v4l2_capability caps = {0};
	struct v4l2_format fmt = { .type = type };
	struct v4l2_requestbuffers req = { .type = type, .count = NBUFS,
	    .memory = V4L2_MEMORY_MMAP };
	struct v4l2_buffer done = { .type = type, .memory = V4L2_MEMORY_MMAP };
	struct mapping buffers[NBUFS] = {{0}};
	int error = 0, streaming = 0;
	int fd = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd == -1)
		return -1;
	if (ioctl(fd, VIDIOC_QUERYCAP, &caps) == -1 || ioctl(fd, VIDIOC_G_FMT, &fmt) == -1)
		goto fail;
	const uint32_t needed = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	const struct v4l2_pix_format *p = &fmt.fmt.pix;
	if ((caps.capabilities & needed) != needed || p->width != WORKSHOP_CAMERA_WIDTH ||
	    p->height != WORKSHOP_CAMERA_HEIGHT || p->pixelformat != V4L2_PIX_FMT_UYVY ||
	    (p->bytesperline != 0 && p->bytesperline != WORKSHOP_CAMERA_STRIDE) ||
	    p->sizeimage != WORKSHOP_CAMERA_BYTES) {
		errno = ENOTSUP;
		goto fail;
	}
	if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1)
		goto fail;
	if (req.count != NBUFS) {
		errno = ENOBUFS;
		goto fail;
	}
	for (unsigned i = 0; i < NBUFS; i++) {
		struct v4l2_buffer b = { .type = type, .memory = V4L2_MEMORY_MMAP, .index = i };
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) == -1)
			goto fail;
		if (b.length < WORKSHOP_CAMERA_BYTES || b.length > WORKSHOP_CAMERA_BYTES + 65536U) {
			errno = EIO;
			goto fail;
		}
		void *addr = mmap(NULL, b.length, PROT_READ, MAP_SHARED, fd, (off_t)b.m.offset);
		if (addr == MAP_FAILED)
			goto fail;
		buffers[i] = (struct mapping){ addr, b.length };
		if (ioctl(fd, VIDIOC_QBUF, &b) == -1)
			goto fail;
	}
	if (ioctl(fd, VIDIOC_STREAMON, &type) == -1)
		goto fail;
	streaming = 1;
	if (capture_buffer(fd, &done) == -1)
		goto fail;
	if (done.index >= NBUFS || done.bytesused != WORKSHOP_CAMERA_BYTES ||
	    (done.flags & V4L2_BUF_FLAG_ERROR) != 0) {
		errno = EIO;
		goto fail;
	}
	if (workshop_camera_write_bmp(bmp_fd, buffers[done.index].data, done.bytesused) == -1)
		goto fail;
	if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1)
		goto fail;
	streaming = 0;
	goto out;
fail:
	error = errno;
out:
	if (streaming)
		(void)ioctl(fd, VIDIOC_STREAMOFF, &type);
	for (unsigned i = 0; i < NBUFS; i++) {
		if (buffers[i].length != 0)
			(void)munmap(buffers[i].data, buffers[i].length);
	}
	if (close(fd) == -1 && error == 0)
		error = errno;
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}
