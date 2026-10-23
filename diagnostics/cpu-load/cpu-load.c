/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted by OpenAI Codex (GPT-6). */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

struct load_state {
    uint64_t a, b;
    double x, y;
};
#define INITIAL_STATE { UINT64_C(0x123456789abcdef), 0, 0.375, 0.625 }

static int
parse_seconds(const char *text, unsigned int *seconds)
{
    const char *p;
    char *end;
    long value;

    if (*text == '\0')
        return -1;
    for (p = text; *p != '\0'; p++)
        if (*p < '0' || *p > '9')
            return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < 1 || value > 620)
        return -1;
    *seconds = (unsigned int)value;
    return 0;
}

static int
reached(const struct timespec *now, const struct timespec *deadline)
{
    return now->tv_sec > deadline->tv_sec ||
        (now->tv_sec == deadline->tv_sec && now->tv_nsec >= deadline->tv_nsec);
}

static void
load_steps(struct load_state *state, unsigned int count)
{
    unsigned int i;

    for (i = 0; i < count; i++) {
        state->a ^= state->a << 13;
        state->a ^= state->a >> 7;
        state->a ^= state->a << 17;
        state->b += state->a * UINT64_C(6364136223846793005) + i;
        state->x = state->x * 0.9999997 + 0.0000001;
        state->y = state->y * 0.9999993 + 0.0000002 + state->x * 0.00000001;
    }
}

static int
valid_fp(const struct load_state *state)
{
    return isfinite(state->x) && isfinite(state->y) &&
        state->x > 0.0 && state->x < 1.0 && state->y > 0.0 && state->y < 1.0;
}

int
main(int argc, char **argv)
{
    struct timespec deadline, now;
    struct load_state state = INITIAL_STATE;
    uint64_t rounds = 0;
    unsigned int seconds;

    if (argc != 2 || parse_seconds(argv[1], &seconds) != 0) {
        fprintf(stderr, "usage: cpu-load seconds (1..620)\n");
        return 2;
    }
    alarm(seconds + 5);
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return 2;
    deadline.tv_sec += seconds;
    do {
        load_steps(&state, 1000000);
        rounds++;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return 2;
    } while (!reached(&now, &deadline));
    printf("COMPUTE pid=%ld rounds=%llu checksum=%llu fp=%.12f,%.12f\n",
        (long)getpid(), (unsigned long long)rounds,
        (unsigned long long)(state.a ^ state.b), state.x, state.y);
    return valid_fp(&state) ? 0 : 3;
}
