/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted by OpenAI Codex (GPT-6). */
#include <sys/wait.h>
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

static int frozen;
static int test_clock(clockid_t, struct timespec *);
#define clock_gettime test_clock
#define main cpu_main
#include "cpu-load.c"
#undef main
#undef clock_gettime

static int
test_clock(clockid_t id, struct timespec *value)
{
    if (frozen == 2)
        return -1;
    if (frozen == 1) {
        value->tv_sec = value->tv_nsec = 0;
        return 0;
    }
    return clock_gettime(id, value);
}

int
main(void)
{
    const char *invalid[] = { "", "0", "-1", "+1", " 1", "1 ", "1x",
        "621", "99999999999999999999999999999999" };
    struct timespec deadline = { 11, 750000000 };
    struct timespec now = { 11, 749999999 };
    struct load_state state = INITIAL_STATE;
    unsigned int seconds;
    size_t i;
    pid_t pid;
    int status;

    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(parse_seconds(invalid[i], &seconds) == -1);
    assert(parse_seconds("1", &seconds) == 0 && seconds == 1);
    assert(parse_seconds("620", &seconds) == 0 && seconds == 620);
    assert(!reached(&now, &deadline));
    now.tv_nsec++;
    assert(reached(&now, &deadline));
    now.tv_sec = 12;
    now.tv_nsec = 0;
    assert(reached(&now, &deadline));
    now.tv_sec = 10;
    now.tv_nsec = 999999999;
    assert(!reached(&now, &deadline));
    load_steps(&state, 1);
    assert(state.a == UINT64_C(4550888344034083252));
    assert(state.b == UINT64_C(5639537809260976292));
    assert(fabs(state.x - 0.3749999875) < 1e-14);
    assert(fabs(state.y - 0.6249997662499999) < 1e-14);
    assert(valid_fp(&state));
    state = (struct load_state)INITIAL_STATE;
    load_steps(&state, 1000000);
    assert((state.a ^ state.b) == UINT64_C(3634307294816191314));
    assert(fabs(state.x - 0.3642007578234592) < 1e-10);
    assert(fabs(state.y - 0.45685016667629286) < 1e-10);
    state.x = NAN;
    assert(!valid_fp(&state));
    state.x = INFINITY;
    assert(!valid_fp(&state));
    state = (struct load_state)INITIAL_STATE;
    state.y = -1.0;
    assert(!valid_fp(&state));

    {
        char *args[] = { "cpu-load", "1", NULL };
        frozen = 2;
        assert(cpu_main(2, args) == 2);
        alarm(0);
        frozen = 0;
    }

    /* A frozen monotonic source cannot bypass the real alarm watchdog. */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        char *args[] = { "cpu-load", "1", NULL };
        frozen = 1;
        _exit(cpu_main(2, args));
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM);
    puts("PASS: parser, nanosecond deadline, integer/FP fixtures and real alarm");
    return 0;
}
