/* Origin: EmberBSD; AI-assisted EGL/GBM pixel readback check. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>

#include "egl-readback-checks.h"

static void
egl_failure(const char *operation)
{

	errx(1, "%s: EGL error 0x%x", operation, eglGetError());
}

static GLuint
compile_shader(GLenum type, const char *source)
{
	GLint status;
	GLuint shader;

	shader = glCreateShader(type);
	if (shader == 0)
		errx(1, "glCreateShader failed");
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1024] = {0};

		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		glDeleteShader(shader);
		errx(1, "shader compilation failed: %s", log);
	}
	return shader;
}

static GLuint
shader_program(void)
{
	static const char vertex[] =
	    "attribute vec2 position; void main() { gl_Position=vec4(position,0.,1.); }";
	static const char fragment[] =
	    "precision mediump float; uniform vec4 color; void main() { gl_FragColor=color; }";
	GLuint vs, fs, program;
	GLint status;

	vs = compile_shader(GL_VERTEX_SHADER, vertex);
	fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
	program = glCreateProgram();
	if (program == 0)
		errx(1, "glCreateProgram failed");
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glBindAttribLocation(program, 0, "position");
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1024] = {0};

		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		glDeleteProgram(program);
		errx(1, "shader link failed: %s", log);
	}
	return program;
}

static void
paint(unsigned frame, GLuint program, GLint color_location)
{
	static const GLfloat positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
	unsigned quadrant;

	glViewport(0, 0, SIDE, SIDE);
	glDisable(GL_DITHER);
	glDisable(GL_BLEND);
	if (program != 0) {
		glUseProgram(program);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
		glEnableVertexAttribArray(0);
	}
	glEnable(GL_SCISSOR_TEST);
	for (quadrant = 0; quadrant < 4; quadrant++) {
		const unsigned char *color = readback_colors[(quadrant + frame) % 4];
		glScissor((quadrant % 2) * (SIDE / 2),
		    (quadrant / 2) * (SIDE / 2), SIDE / 2, SIDE / 2);
		if (program != 0) {
			glUniform4f(color_location, color[0] / 255.f, color[1] / 255.f,
			    color[2] / 255.f, 1.f);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		} else {
			glClearColor(color[0] / 255.f, color[1] / 255.f, color[2] / 255.f, 1.f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
	}
	glDisable(GL_SCISSOR_TEST);
	if (program != 0)
		glDisableVertexAttribArray(0);
}

static void
write_ppm(const char *path, const unsigned char *pixels)
{
	FILE *file;
	int fd, x, y;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd == -1)
		err(1, "create image %s", path);
	file = fdopen(fd, "wb");
	if (file == NULL) {
		close(fd);
		err(1, "fdopen image");
	}
	if (fprintf(file, "P6\n%d %d\n255\n", SIDE, SIDE) < 0)
		err(1, "write image header");
	/* PPM starts at the top row; GL readback starts at the bottom row. */
	for (y = SIDE - 1; y >= 0; y--) {
		for (x = 0; x < SIDE; x++) {
			if (fwrite(pixels + (y * SIDE + x) * 4, 1, 3, file) != 3)
				err(1, "write image pixels");
		}
	}
	if (fclose(file) != 0)
		err(1, "close image");
}

int
main(int argc, char **argv)
{
	EGLint config_attributes[] = {
		EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8, EGL_NONE
	};
	static const EGLint context_attributes[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
	};

	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
	struct gbm_device *gbm = NULL;
	EGLDisplay display;
	EGLContext context;
	EGLSurface surface = EGL_NO_SURFACE;
	EGLConfig config;
	EGLint count, major, minor;
	GLuint texture, framebuffer, program = 0;
	GLint color_location = -1;
	unsigned char pixels[SIDE * SIDE * 4];
	const char *renderer;
	int fd = -1, software, zink, frame;
	size_t bad;

	if (argc != 2 && argc != 4)
		errx(2, "usage: egl-readback DRM_RENDER_NODE | --software | "
		    "--zink EXPECTED_DEVICE_NAME NEW_PPM");
	zink = strcmp(argv[1], "--zink") == 0;
	if ((zink && argc != 4) || (!zink && argc != 2))
		errx(2, "--zink requires the expected Vulkan device name and a new PPM");
	software = strcmp(argv[1], "--software") == 0;
	if (zink) {
		const char *icd = getenv("VK_DRIVER_FILES");
		if (icd == NULL || *icd != '/' || strchr(icd, ':') != NULL)
			errx(1, "--zink requires one absolute VK_DRIVER_FILES manifest");
		if (getenv("VK_ICD_FILENAMES") != NULL ||
		    getenv("VK_ADD_DRIVER_FILES") != NULL ||
		    getenv("VK_ADDITIONAL_DRIVER_FILES") != NULL ||
		    getenv("LIBGL_ALWAYS_SOFTWARE") != NULL)
			errx(1, "remove conflicting ICD and software overrides");
		printf("VK_DRIVER_FILES: %s\n", icd);
		config_attributes[1] = EGL_PBUFFER_BIT;
	}
	get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
	    eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (get_platform_display == NULL)
		errx(1, "EGL_EXT_platform_base is unavailable");
	if (software || zink) {
		display = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA,
		    EGL_DEFAULT_DISPLAY, NULL);
	} else {
		fd = open(argv[1], O_RDWR | O_CLOEXEC);
		if (fd < 0)
			err(1, "open %s", argv[1]);
		gbm = gbm_create_device(fd);
		if (gbm == NULL)
			errx(1, "gbm_create_device failed");
		display = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	}
	if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
		egl_failure("eglInitialize");
	if (!eglBindAPI(EGL_OPENGL_ES_API))
		egl_failure("eglBindAPI");
	if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
		egl_failure("eglChooseConfig");
	context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
	if (context == EGL_NO_CONTEXT)
		egl_failure("eglCreateContext");
	if (zink) {
		const EGLint attributes[] = { EGL_WIDTH, SIDE, EGL_HEIGHT, SIDE, EGL_NONE };
		surface = eglCreatePbufferSurface(display, config, attributes);
		if (surface == EGL_NO_SURFACE)
			egl_failure("eglCreatePbufferSurface");
	}
	if (!eglMakeCurrent(display, surface, surface, context))
		egl_failure("eglMakeCurrent");
	renderer = (const char *)glGetString(GL_RENDERER);
	if (renderer == NULL)
		errx(1, "GL_RENDERER unavailable");
	printf("EGL %d.%d\nGL_VENDOR: %s\nGL_RENDERER: %s\nGL_VERSION: %s\n",
	    major, minor, glGetString(GL_VENDOR), renderer, glGetString(GL_VERSION));
	if (!software && !zink && (strstr(renderer, "virgl") == NULL ||
	    strstr(renderer, "llvmpipe") != NULL || strstr(renderer, "softpipe") != NULL))
		errx(1, "a VirGL renderer is required for the GPU check");

	if (zink) {
		if (!readback_zink_renderer(renderer, argv[2]))
			errx(1, "Zink on the expected hardware renderer is required");
		program = shader_program();
		color_location = glGetUniformLocation(program, "color");
		if (color_location < 0)
			errx(1, "shader color uniform is unavailable");
		paint(0, program, color_location);
		glFinish();
		memset(pixels, 0x5a, sizeof(pixels));
		glReadPixels(0, 0, SIDE, SIDE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		if (glGetError() != GL_NO_ERROR || !readback_pixels(pixels, 0, &bad))
			errx(1, "pbuffer shader pixel readback failed");
		write_ppm(argv[3], pixels);
	}

	/* Reallocate and read every pixel, exercising object and transfer lifetime. */
	for (frame = 0; frame < 64; frame++) {
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIDE, SIDE, 0,
		    GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		    GL_TEXTURE_2D, texture, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			errx(1, "incomplete framebuffer at frame %d", frame);
		paint((unsigned)frame, program, color_location);
		glFinish();
		memset(pixels, 0x5a, sizeof(pixels));
		glReadPixels(0, 0, SIDE, SIDE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		if (glGetError() != GL_NO_ERROR)
			errx(1, "GL operation failed at frame %d", frame);
		if (!readback_pixels(pixels, (unsigned)frame, &bad))
			errx(1, "frame %d pixel %zu channel %zu differs from golden",
			    frame, bad / 4, bad % 4);
		glDeleteFramebuffers(1, &framebuffer);
		glDeleteTextures(1, &texture);
	}
	if (program != 0)
		glDeleteProgram(program);
	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
		egl_failure("release context");
	if (!eglDestroyContext(display, context))
		egl_failure("eglDestroyContext");
	if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface))
		egl_failure("eglDestroySurface");
	if (!eglTerminate(display))
		egl_failure("eglTerminate");
	if (gbm != NULL)
		gbm_device_destroy(gbm);
	if (fd >= 0)
		close(fd);
	printf("PASS: 64 allocations, four-color rendering and complete pixel readback (%s).\n",
	    software ? "software plumbing only" :
	    zink ? "Zink; shader pbuffer and FBO pixels" : "VirGL; verify host GPU separately");
	return 0;
}
