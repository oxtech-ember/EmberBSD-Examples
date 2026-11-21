#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import hashlib
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parent / "upstream" / "qtdeclarative-v6.12.0"
manifest = json.loads((root / "UPSTREAM.json").read_text(encoding="utf-8"))
failed = False
for name, record in manifest["files"].items():
    digest = hashlib.sha256((root / name).read_bytes()).hexdigest()
    if digest != record["sha256"]:
        print(f"FAIL: {name}: SHA256 {digest}, expected {record['sha256']}", file=sys.stderr)
        failed = True
if failed:
    raise SystemExit(1)
print(f"PASS: {len(manifest['files'])} upstream inputs match Qt {manifest['tag']} ({manifest['commit']})")
