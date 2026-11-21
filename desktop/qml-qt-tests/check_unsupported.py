#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import re
import sys

if len(sys.argv) != 5:
    raise SystemExit("usage: check_unsupported.py stderr-file generated-c-file source-name expected-diagnostic")
error_path = Path(sys.argv[1])
generated_path = Path(sys.argv[2])
source_name, expected_diagnostic = sys.argv[3:]
message = error_path.read_text(encoding="utf-8", errors="replace")
if generated_path.exists():
    raise SystemExit(f"FAIL: rejected {source_name} input created a C output")
if f"{source_name}:" not in message or expected_diagnostic not in message:
    raise SystemExit(f"FAIL: expected source-local diagnostic for {expected_diagnostic!r}")
if re.search(rf"{re.escape(source_name)}:[1-9][0-9]*:[1-9][0-9]*:", message) is None:
    raise SystemExit("FAIL: diagnostic is missing a source line")
print(f"PASS: {source_name} is rejected with a source-local diagnostic before C generation")
