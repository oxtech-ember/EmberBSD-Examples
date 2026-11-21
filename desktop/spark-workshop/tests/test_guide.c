/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../guide.h"

static void
test_guide(const char *path)
{
	struct workshop_guide guide;
	assert(workshop_guide_load(path, &guide) == 0);
	assert(strcmp(guide.title, "Connector inspection") == 0);
	assert(guide.step_count == 3);
	assert(strcmp(guide.steps[1].title, "Inspect the latch") == 0);
	assert(strcmp(guide.steps[1].paragraph,
	    "Find the locking tab. Take a photo that shows the tip of the tab and its base.") == 0);
	assert(guide.steps[1].line_count >= 2 && guide.steps[1].line_count <= 3);
}

static void
test_reject(const char *text)
{
	struct workshop_guide guide;
	char path[] = "/tmp/spark-workshop-guide.XXXXXX";
	size_t length = strlen(text), offset = 0;
	int fd = mkstemp(path);
	int result;
	assert(fd != -1);
	while (offset < length) {
		ssize_t count = write(fd, text + offset, length - offset);
		assert(count > 0);
		offset += (size_t)count;
	}
	assert(close(fd) == 0);
	result = workshop_guide_load(path, &guide);
	if (result != -1 || errno != EINVAL) {
		fprintf(stderr, "invalid guide accepted or wrong errno: %s (result=%d errno=%d)\n",
		    text, result, errno);
		abort();
	}
	assert(unlink(path) == 0);
}

int
main(int argc, char **argv)
{
	assert(argc == 2);
	test_guide(argv[1]);
	test_reject("# Empty\n\n## One\nFirst paragraph.\n\nSecond paragraph.\n");
	test_reject("# Empty\n\n## One\n\n");
	test_reject("# Title\n\n## Step\nBody\n\n## Second\n");
	test_reject("# Too long\n\n## Step\nThiswordisfarbeyondthemaximumsupportedparagraphlinewidth\n");
	assert(puts("guide parser: PASS") >= 0);
	return 0;
}
