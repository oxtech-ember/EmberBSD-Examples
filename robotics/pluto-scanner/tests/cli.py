#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run bounded CLI checks; no receiver or substituted system calls are used."""
import subprocess
import sys

binary = sys.argv[1]
missing = '/nonexistent-pluto-scanner-receiver'
cases = [
    (['--help'], 0),
    ([missing, '3800000000', '3800000000'], 2),
    ([missing, '325000000', '3800000000'], 1),
    ([missing, '-1', '3800000000'], 2),
    ([missing, '137100000', '137900000'], 1),
    ([missing, '70000000', '6000000000'], 1),
    ([missing, '69999999', '3800000000'], 2),
    ([missing, '325000000', '6000000001'], 2),
    ([missing, '3800000000', '325000000'], 2),
    ([missing, '', '3800000000'], 2),
    ([missing, '325000000', '999999999999999999999999999'], 2),
    (['/dev/null'], 1),
]
for args, expected in cases:
    result = subprocess.run([binary] + args, capture_output=True, timeout=5)
    assert result.returncode == expected, (args, result.returncode, result.stderr)
    if '/dev/null' in args:
        assert result.stdout == b'center_hz,noise_dbfs,peak_hz,peak_dbfs,rssi1_mdB,gain1_mdB\n'
print('PASS CLI endpoints, malformed input, unavailable device, and failed real ioctl')
