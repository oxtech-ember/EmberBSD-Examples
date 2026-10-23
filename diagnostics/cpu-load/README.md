# Bounded CPU load

This small C example runs integer and floating-point arithmetic for 1 to 620
seconds. It uses one CPU thread and negligible working memory. Run separate
processes under the system's normal CPU affinity and priority tools when a
controller needs several workers. It does not control affinity, temperatures,
GPU, NPU, RAM allocation or system policy.

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -ffp-contract=off \
  -D_POSIX_C_SOURCE=200809L cpu-load.c -o cpu-load
./cpu-load 10
```

The argument contains decimal digits only. Empty, signed, malformed,
overflowing and out-of-range values exit with status 2. A monotonic deadline
preserves nanoseconds. The workload checks time after each batch of one
million iterations, so completion can exceed the deadline by one batch and
scheduler delay. An independent alarm terminates the process after another
five seconds if the normal clock path stalls. Clock failures exit with 2.
A failed finite/range check on the floating-point results exits with 3.

Each completed run prints `COMPUTE`, PID, batch count, integer checksum and
floating-point results. The arithmetic retains the original diagnostic's
unsigned xorshift/multiply and bounded double recurrences. The unsigned
operations wrap by C's defined modulo arithmetic. Floating-point contraction
and fast-math must remain disabled for the supplied fixtures.

## Checks

```sh
python3 check.py --cc /absolute/gcc16 --work /absolute/new-check
```

The regression includes the actual production source. Literal scalar fixtures
check one iteration and one complete batch. Floating-point comparisons use
independent expected values and tolerances. Tests cover argument parsing,
nanosecond boundaries, clock failure, nonfinite results and a frozen-clock
child that must die from the real alarm. A real one-second CLI run checks the
deadline and output. Four deliberate mutations exercise range validation,
deadline comparison, integer arithmetic and floating-point arithmetic.

GCC16 native NetBSD checks pass. The same compiler cross-builds both production
and test programs for the matching EmberBSD AArch64 sysroot. GCC path analysis
and Clang Static Analyzer complete without diagnostics. Cppcheck 2.22.0 on
actual target preprocessing retains a false errno model notice and a portable
wait-status assertion style notice. They remain visible; no category is
suppressed. This does not establish target execution or thermal acceptance.

The native Linux AArch64 [CI workflow](../../.github/workflows/cpu-load-linux-aarch64.yml)
uses a digest-pinned official GCC 16.2.0 container. It runs all regressions,
strict Valgrind Memcheck and GCC ASan/UBSan. The downloadable diagnostic is
static ELF64 AArch64, machine 183. Its artifact includes source, BSD license,
libc notices, source pointers, commands, hashes and test evidence. Linux
execution does not establish EmberBSD ABI or physical-board acceptance.

The code is BSD-2-Clause. Origin: EmberBSD contributors, AI-assisted by OpenAI
Codex (GPT-6). It does not depend on private files or device names.
