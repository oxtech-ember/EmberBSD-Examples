#!/usr/bin/env python3
# Origin: EmberBSD, AI-assisted by OpenAI Codex (GPT-6).
"""Build and check the bounded CPU diagnostic with GCC16."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import platform
import struct
import resource
import shutil
import signal
import subprocess
import time

HERE = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--work", type=Path, required=True)
    p.add_argument("--cc", default="gcc")
    p.add_argument("--flag", action="append", default=[])
    p.add_argument("--artifact", type=Path)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=False)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    record = {"status": "started", "commands": [], "inputs": {
        n: hashlib.sha256((HERE / n).read_bytes()).hexdigest()
        for n in ("cpu-load.c", "test.c", "check.py")}}

    def run(cmd, name, expected=0, timeout=12):
        start = time.monotonic()
        proc = subprocess.Popen([str(x) for x in cmd], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        try:
            output, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.communicate()
            raise RuntimeError(name + " exceeded the execution bound")
        elapsed = time.monotonic() - start
        (a.work / (name + ".log")).write_bytes(output)
        record["commands"].append({"argv": [str(x) for x in cmd],
                                   "status": proc.returncode, "seconds": elapsed})
        if expected is not None and proc.returncode != expected:
            raise RuntimeError(f"{name}: exit {proc.returncode}")
        return proc.returncode, elapsed, output

    try:
        _, _, version = run([a.cc, "--version"], "compiler")
        if "16." not in version.decode().splitlines()[0]:
            raise RuntimeError("GCC16 is required")
        flags = ["-std=c11", "-O2", "-g", "-gdwarf-4", "-Wall", "-Wextra", "-Werror",
                 "-fanalyzer", "-ffp-contract=off", "-D_POSIX_C_SOURCE=200809L", *a.flag]
        for name in ("cpu-load", "test"):
            run([a.cc, *flags, HERE / (name + ".c"), "-o", a.work / name], name + "-build")
        run([a.work / "test"], "unit")
        for index, args in enumerate(([], [""], ["0"], ["-1"], ["+1"], [" 1"],
                                      ["1x"], ["621"], ["999999999999999999999999"], ["1", "2"])):
            run([a.work / "cpu-load", *args], "args-" + str(index), expected=2)
        _, elapsed, output = run([a.work / "cpu-load", "1"], "runtime", timeout=6)
        if not 0.95 <= elapsed < 5.5 or not re.search(rb"rounds=[1-9][0-9]* checksum=[0-9]+ fp=", output):
            raise RuntimeError("runtime deadline or result contract failed")
        text = (HERE / "cpu-load.c").read_text()
        mutations = {
            "range": ("value > 620", "value > 621"),
            "deadline": ("now->tv_nsec >= deadline->tv_nsec", "now->tv_nsec >= 0"),
            "checksum": ("6364136223846793005", "6364136223846793007"),
            "fp": ("0.9999997", "0.9999977"),
        }
        for name, (old, new) in mutations.items():
            d = a.work / name
            d.mkdir()
            if text.count(old) != 1:
                raise RuntimeError(name + " mutation site mismatch")
            (d / "cpu-load.c").write_text(text.replace(old, new))
            shutil.copy2(HERE / "test.c", d / "test.c")
            run([a.cc, *flags, d / "test.c", "-o", d / "test"], name + "-build")
            code, _, data = run([d / "test"], name, expected=None)
            if code != -signal.SIGABRT or b"assert" not in data.lower():
                raise RuntimeError(name + " mutation was not detected")
        record["binary_sha256"] = hashlib.sha256((a.work / "cpu-load").read_bytes()).hexdigest()
        if a.artifact:
            if platform.system() != "Linux" or platform.machine() != "aarch64":
                raise RuntimeError("Linux ARM64 artifact requires native Linux ARM64")
            a.artifact.mkdir(parents=True, exist_ok=False)
            binary = a.artifact / "cpu-load"
            run([a.cc, *flags, "-static", HERE / "cpu-load.c", "-o", binary], "static-build")
            data = binary.read_bytes()
            if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", data, 16) != (2, 183):
                raise RuntimeError("expected static ELF64 AArch64 ET_EXEC")
            phoff = struct.unpack_from("<Q", data, 32)[0]
            size, number = struct.unpack_from("<HH", data, 54)
            if any(struct.unpack_from("<I", data, phoff + i * size)[0] in (2, 3) for i in range(number)):
                raise RuntimeError("unexpected dynamic segment or interpreter")
            run([binary, "1"], "static-runtime", timeout=6)
            record["artifact"] = {"sha256": hashlib.sha256(data).hexdigest(),
                "size": len(data), "elf_machine": 183, "static": True,
                "commit": os.environ["GITHUB_SHA"], "image": os.environ["GCC_IMAGE"]}
            record["runtime_packages"] = run(["dpkg-query", "-W", "libc6", "libc6-dev"], "libc-packages")[2].decode()
            record["gcc_sha256"] = hashlib.sha256(Path(shutil.which(a.cc)).resolve().read_bytes()).hexdigest()
            (a.artifact / "SHA256SUMS").write_text(record["artifact"]["sha256"] + "  cpu-load\n")
            for name in ("cpu-load.c", "test.c", "check.py", "LICENSE", "README.md"):
                shutil.copy2(HERE / name, a.artifact / name)
            shutil.copy2("/usr/share/doc/libc6/copyright", a.artifact / "libc-copyright")
            record["runtime_sources"] = {"glibc": "https://sources.debian.org/src/glibc/",
                "gcc": "https://gcc.gnu.org/pub/gcc/releases/gcc-16.2.0/"}
        record["status"] = "pass"
        print("PASS: argument/clock/integer/FP/alarm cases, runtime and four causal mutations")
    finally:
        (a.work / "receipt.json").write_text(json.dumps(record, indent=2) + "\n")
        if a.artifact and a.artifact.exists():
            (a.artifact / "receipt.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
