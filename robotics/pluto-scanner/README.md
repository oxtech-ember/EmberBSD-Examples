# Pluto RX frequency scanner

This C11 example makes one receive-only sweep through 325 MHz to 3.8 GHz.
It needs the EmberBSD ADI AXI ADC receive ABI version 1 and `/dev/adiadc0`.
It has no transmit controls and does not decode received communications.

## Build and run

Select GCC16 and a matching EmberBSD application sysroot. The sysroot must
install `dev/fdt/adi_axi_adc.h`. A kernel-only compiler does not supply libc.

```sh
make CC=/absolute/armv7--netbsdelf-eabihf-gcc \
  CPPFLAGS='--sysroot=/absolute/earmv7hf-sysroot' \
  LDFLAGS='--sysroot=/absolute/earmv7hf-sysroot'
./pluto-scan --help
./pluto-scan /dev/adiadc0 325000000 3800000000 > sweep.csv
```

The default plan uses 10 MSPS, 8 MHz bandwidth, 3 MHz tune steps and 4096
complex samples per capture. The last tune reaches the exact upper endpoint.
The default sweep contains 1160 centers. Pass a smaller interval in hertz
for a shorter scan. Both endpoints must remain within the supported band.

The program opens the receiver read only. It configures RX1, captures finite
IQ, reads the complete sample buffer, then checks final capture status. A capture
error stops the sweep. It never publishes peaks from an earlier capture.
Ctrl-C closes the session. An unsupported driver returns an error; the
program does not bypass hardware quiescence or DMA ownership checks.

## CSV output

Columns contain the tune center, noise floor, peak frequency, peak level,
RX1 RSSI and RX1 gain. A center can produce up to five rows. Empty peak
fields mean that no peak passed the threshold. RSSI and gain fields remain
empty when the driver does not mark them valid.

The DSP core sign-extends low-12-bit I and Q, applies a periodic Hann window,
and computes a radix-2 complex FFT. Coherent gain sets the amplitude scale.
Levels use relative dBFS per FFT bin. They are not calibrated dBm readings.
The noise estimate is the median valid-bin power. Peaks must exceed it by
10 dB and must be at least three bins apart. DC and its immediate neighbors,
band edges and frequencies outside the requested interval are excluded.
The tune overlap covers the excluded DC region through adjacent captures.
Plans must span more than two FFT bins. Boundary estimates within half a bin
are clipped to the requested endpoint; frequency resolution remains one bin.

## Checks and current limits

```sh
CC=/absolute/gcc16 CFLAGS='host ABI flags' sh tests/run.sh
```

The production DSP code passes synthetic positive and negative tone tests,
known amplitude checks, zero input, invalid plans and sizes, endpoint and
overlap checks, and stale-result rejection after a failed capture. GCC16
ASan/UBSan checks pass on macOS arm64. Cppcheck and GCC path analysis complete
without diagnostics. Native compilation uses the actual receive ABI header.
Host CLI checks cover exact endpoints, malformed input and failed real ioctl.

A static ARMv7 hard-float build ran on a PlutoSDR Nano from RAM on 2026-10-24.
The receiver used EmberBSD commit `10d9f2d024ab1a333aee740a083ccf4f7fa6ce1b`,
configuration `PLUTO_NANO_RX`, and the original device FPGA image in volatile fabric.
One complete default sweep returned zero and covered all 1160 centers.
A second 935–960 MHz sweep also returned zero with different tune centers.
Fresh hardware snapshots showed TX channel bits clear and the TX synthesizer inhibited.
These observations establish finite RX and sweep execution on that configuration.
They do not qualify continuous streaming, RF calibration, antenna coverage,
frequency accuracy, USB networking, persistent installation or other Nano revisions.
The local GCC16 build was an authorized experiment, not a qualified SDK release.
See the [board profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/tree/main/boards/hamgeek-plutosdr-nano)
for boot and hardware limits.

A RAM root can start read only. Before redirecting CSV to its `/tmp` directory,
run `mount -u -w /`. This changes the RAM filesystem only.
Copy results to the host before rebooting; volatile files do not survive reset.
Host checks alone do not establish reception.
The source is BSD-2-Clause and was AI-assisted by OpenAI Codex (GPT-6).
