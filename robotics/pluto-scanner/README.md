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
for a shorter scan. Explicit endpoints can span 70 MHz to 6 GHz if the receiver driver permits them.
The default sweep stays within 325 MHz to 3.8 GHz. AD9363 receivers require
the explicit `emberbsd,experimental-rx-extended-range` FDT property for tuning
outside that default band. This is experimental tuning, not RF calibration.
The AD9363 20 MHz RF bandwidth limit remains in force.

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

## One finite raw IQ block

`pluto-iq` uses the same receive-only ABI. It does not change the scanner,
its 10 MSPS defaults, FFT processing, or CSV output.

```sh
./pluto-iq /dev/adiadc0 1090000000 2400000 2000000 262144 > adsb.iqhex
./pluto-iq /dev/adiadc0 162000000 3072000 600000 262144 > ais.iqhex
```

The arguments are device, frequency, sample rate, bandwidth, and complex sample count.
Numbers must contain decimal digits only. Frequency must be 70 MHz through 6 GHz.
Rate and bandwidth must be positive and fit unsigned 32-bit integers.
The driver checks whether the requested RF configuration is supported.
Count must be even, from 2 through 262144. This bounds allocation to 1 MiB
and makes the capture size a multiple of eight bytes.
The no-FIR clock chain does not support 1.536 MSPS. Use 3.072 MSPS for this AIS experiment.
These commands request settings; they do not establish the actual sample clock or RF reception.

Each invocation opens one session, sends CONFIG and CAPTURE, then reads the complete buffer.
The first read requests all capture bytes because ABI1 starts DMA in that read.
An interrupted read retries the full request. A short read, EOF, or other error rejects the block.
The helper checks fresh STATUS after reading, then closes the device before writing IQ.
A failed capture, mismatched ABI, count, state, or final capture error produces no IQ output.
Errors go to standard error. Invalid arguments return 2; device and output failures return 1.

The ASCII format is version 1. This example has two complex samples:

```text
PLUTO-IQ 1
BEGIN block=0 discontinuity=1
REQUEST frequency_hz=1090000000 sample_rate_hz=2400000 bandwidth_hz=2000000
FORMAT s12_le16_iq bytes_per_sample=4
COUNT samples=2 bytes=8
DATA offset=00000000 0001020304050607
END block=0 samples=2 bytes=8 status=ok
```

Lines end with LF. The receiver console can convert LF to CRLF during transfer.
Each DATA line has an eight-digit lowercase hexadecimal byte offset and one space
before the payload. Payload contains lowercase hexadecimal bytes, with no separators.
Each line contains 32 bytes except the final line, which contains the remainder.
Offsets start at zero and increase by 32. The final offset plus row length must equal COUNT bytes.
END repeats the exact sample and byte counts. No bytes follow END in the helper output.
A consumer must check every offset, payload length, count, and complete END line.
A local file also requires exit status zero. An incomplete serial transfer must be rejected.

Bytes retain device order: I low byte, I high byte, Q low byte, Q high byte.
Each component is signed 12-bit two's complement in a little-endian 16-bit container.
Sign-extend its low 12 bits before processing; do not assume the upper four bits are sign bits.
REQUEST fields contain requested values. ABI1 does not return an actual sample rate.
The footer confirms finite capture completion, not a measured clock or a TX register snapshot.
Capture STATUS and a TX-inhibit hardware snapshot remain separate acceptance evidence.

Every invocation has block index zero and declares a discontinuity before its data.
Keep each block in its own file and decode it independently. Do not concatenate blocks
into a continuous stream or hide capture gaps through resampling.
The maximum block lasts about 109 ms at 2.4 MSPS, or 85 ms at 3.072 MSPS.
At 115200 baud with 8N1 framing, its ASCII transfer takes about 4.1 minutes.
This helper is a finite diagnostic. It does not implement USB streaming.

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
Finite IQ adapter tests exercise the production CLI with device system-call fixtures.
They cover full-buffer first reads, maximum count, byte alignment, indexed framing,
strict arguments, interrupted reads, short reads, capture errors, and stale-status refusal.
They also check the separate ADS-B and AIS request settings and output-error propagation.

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
