# I²C register read

A small EmberBSD example for reading bytes from an explicitly selected I²C
slave through `/dev/iic` and `I2C_IOCTL_EXEC`. The arguments are device path,
7-bit address, register width in bytes (1 or 2), register value, and byte count
(1 to 256). Numbers are decimal or C-style hexadecimal. A two-byte register
is sent most significant byte first. The transfer writes the register pointer
and then reads with a stop, using the controller's combined transaction.
The returned bytes are printed in hexadecimal.

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror i2c-register-read.c -o i2c-register-read
./i2c-register-read /dev/iic0 0x20 1 0x10 2
```

Use only registers and devices whose documentation permits reading: reads can
have side effects, and this example does not detect devices or manage power,
reset, pinmux or concurrent kernel drivers. Transfer support depends on the
I²C controller. Errors are reported and return nonzero. Device execution has
not been verified by the included host regression.

## Checks

`test.c` includes the production program with controlled `open`, `ioctl` and
`close` functions. It checks the request fields and register byte order for
both widths, a returned transfer error and the count bound; it does not open a
bus or exercise a kernel driver. On a non-EmberBSD host, point `-idirafter` at
the matching EmberBSD headers so its real `i2c_io.h` is used after host system
headers. For example:

```sh
clang -std=c11 -O1 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -idirafter "$EMBERBSD_SYSROOT/usr/include" test.c -o test-i2c-read
./test-i2c-read
```

The example also compiles and statically links for RV32 ILP32 using the
matching EmberBSD C foundation and selected Clang/compiler-rt. The host test
and cross-link do not establish device or controller operation.
