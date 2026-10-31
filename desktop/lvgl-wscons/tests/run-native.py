#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Test real callbacks on the host; wscons constants come from OS headers."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-prefix', required=True, type=Path)
p.add_argument('--lvgl-prefix', required=True, type=Path)
p.add_argument('--wscons-header', required=True, type=Path)
a = p.parse_args()
host = a.host_prefix.resolve(strict=True)
clang = (host / 'bin/clang').resolve(strict=True)
linker = (host / 'bin' / ('ld64.lld' if sys.platform == 'darwin' else 'ld.lld')).resolve(strict=True)
if not clang.is_relative_to(host) or not linker.is_relative_to(host):
    p.error('compiler and linker must be inside host prefix')
env = dict(os.environ, PATH=os.pathsep.join((str(host / 'bin'), str(host / 'gnu/bin'))))
text = a.wscons_header.read_text()
names = ('WSFB_RGB', 'WSCONS_EVENT_MOUSE_UP', 'WSCONS_EVENT_MOUSE_DOWN',
         'WSCONS_EVENT_MOUSE_ABSOLUTE_X', 'WSCONS_EVENT_MOUSE_ABSOLUTE_Y',
         'WSEVENT_VERSION', 'WSDISPLAYIO_MODE_EMUL', 'WSDISPLAYIO_MODE_DUMBFB')
macros = []
for name in names:
    match = re.search(r'^#define\s+' + name + r'\s+([0-9]+)\b', text, re.M)
    if not match:
        p.error('missing constant in supplied OS header: ' + name)
    macros.append('#define ' + name + ' ' + match.group(1))
with tempfile.TemporaryDirectory(prefix='lvgl-wscons-') as tmp:
    tmp = Path(tmp)
    header = tmp / 'dev/wscons/wsconsio.h'
    header.parent.mkdir(parents=True)
    header.write_text('#include <sys/types.h>\n#include <stdint.h>\n#include <time.h>\n' + '\n'.join(macros) + '''
struct wscons_event { unsigned int type; int value; struct timespec time; };
struct wsdisplay_fbinfo { unsigned int height, width, depth, cmsize; };
struct _fbi_rgbmasks { uint32_t red_offset, red_size, green_offset, green_size,
    blue_offset, blue_size, alpha_offset, alpha_size; };
struct wsdisplayio_fbinfo {
    uint64_t fbi_fbsize, fbi_fboffset;
    uint32_t fbi_width, fbi_height, fbi_stride, fbi_bitsperpixel, fbi_pixeltype;
    union { struct _fbi_rgbmasks fbi_rgbmasks; } fbi_subtype;
    uint32_t fbi_flags;
};
#define WSDISPLAYIO_GET_FBINFO 6
#define WSMOUSE_EVENT_VERSION WSEVENT_VERSION
#define WSMOUSEIO_SETVERSION 1
#define WSDISPLAYIO_GINFO 2
#define WSDISPLAYIO_GMODE 3
#define WSDISPLAYIO_LINEBYTES 4
#define WSDISPLAYIO_SMODE 5
''')
    prefix = a.lvgl_prefix.resolve(strict=True)
    exe = tmp / 'test'
    source = Path(__file__).resolve().with_name('memory.c')
    subprocess.run([str(clang), '-fuse-ld=lld', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-g', '-I' + str(tmp),
                    '-I' + str(prefix / 'include'), '-DLV_CONF_INCLUDE_SIMPLE',
                    '-DLV_KCONFIG_IGNORE', str(source), str(prefix / 'lib/liblvgl.a'),
                    '-o', str(exe)], env=env, check=True)
    subprocess.run([str(exe)], env=env, check=True)
