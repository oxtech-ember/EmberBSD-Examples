#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise production IIOD commands through an existing host libiio.

The TCP transport is a loopback test adapter. It does not test USB hardware.
Arguments: synthetic fixture executable, existing libiio shared library.
"""
import ctypes as C
import os
import socket
import subprocess
import sys
import threading

fixture, library = sys.argv[1:]
if sys.platform == "darwin":
    usb = os.path.join(os.path.dirname(library), "libusb-1.0.0.dylib")
    C.CDLL(usb, mode=C.RTLD_GLOBAL)
lib = C.CDLL(library, use_errno=True)
P = C.c_void_p
def api(name, result, *args):
    f = getattr(lib, name)
    f.restype, f.argtypes = result, args
    return f
version = api("iio_library_get_version", None, P, P, P)
major, minor, tag = C.c_uint(), C.c_uint(), C.create_string_buffer(8)
version(C.byref(major), C.byref(minor), tag)
assert (major.value, minor.value) == (0, 25), (major.value, minor.value)
create = api("iio_create_context_from_uri", P, C.c_char_p)
find = api("iio_context_find_device", P, P, C.c_char_p)
channel = api("iio_device_find_channel", P, P, C.c_char_p, C.c_bool)
read = api("iio_channel_attr_read", C.c_ssize_t, P, C.c_char_p, P, C.c_size_t)
write = api("iio_channel_attr_write", C.c_ssize_t, P, C.c_char_p, C.c_char_p)
enable = api("iio_channel_enable", None, P)
size = api("iio_device_get_sample_size", C.c_ssize_t, P)
buffer_create = api("iio_device_create_buffer", P, P, C.c_size_t, C.c_bool)
refill = api("iio_buffer_refill", C.c_ssize_t, P)
begin = api("iio_buffer_start", P, P)
buffer_destroy = api("iio_buffer_destroy", None, P)
destroy = api("iio_context_destroy", None, P)

listener = socket.socket()
listener.bind(("127.0.0.1", 0))
listener.listen(4)
listener.settimeout(0.2)
port = listener.getsockname()[1]
done = threading.Event()
children = []
def serve():
    while not done.is_set():
        try:
            conn, _ = listener.accept()
        except socket.timeout:
            continue
        with conn:
            children.append(subprocess.Popen([fixture], stdin=conn, stdout=conn))
thread = threading.Thread(target=serve)
thread.start()
ctx = buf = None
try:
    ctx = create(f"ip:127.0.0.1:{port}".encode())
    assert ctx, f"context errno={C.get_errno()}"
    phy, adc = find(ctx, b"ad9361-phy"), find(ctx, b"cf-ad9361-lpc")
    assert phy and adc
    rx, lo = channel(phy,b"voltage0",False),channel(phy,b"altvoltage0",True)
    assert rx and lo
    assert not channel(phy,b"voltage0",True)
    assert write(rx,b"sampling_frequency",b"2400000") > 0
    text = C.create_string_buffer(128)
    assert read(rx,b"sampling_frequency",text,128)==7 and text.value==b"2399999"
    assert write(rx,b"sampling_frequency",b"1536000") < 0
    assert write(lo,b"frequency",b"1090000000") > 0
    assert read(lo,b"frequency",text,128)==10 and text.value==b"1090000000"
    enable(channel(adc,b"voltage0",False));enable(channel(adc,b"voltage1",False))
    assert size(adc)==4
    buf = buffer_create(adc,8,False)
    assert buf, f"buffer errno={C.get_errno()}"
    assert refill(buf)==32
    assert C.string_at(begin(buf),32)==bytes([255,15,1,0])*8
    print(f"libiio {major.value}.{minor.value}: XML, attributes, RX mask, buffer protocol PASS")
finally:
    if buf: buffer_destroy(buf)
    if ctx: destroy(ctx)
    done.set();thread.join();listener.close()
    for child in children:
        try: child.wait(timeout=2)
        except subprocess.TimeoutExpired: child.terminate();child.wait()
