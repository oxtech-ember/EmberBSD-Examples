# Native receive-only IIO USB service

`pluto-iiod` maps the libiio 0.25 IIOD protocol to EmberBSD receive devices.
It uses the native USB peripheral ABI and the ADC streaming ABI version 2.
The peripheral driver must implement `USBPERIPH_IOC_ENDPOINT_GATE`.
It has no Linux FunctionFS, sysfs, libiio, or compression dependency on the board.
The runtime dependencies are the base C, math, and pthread libraries.

This is an implementation candidate. USB enumeration, throughput, RF reception,
SDR++ operation, and decoder acceptance still require physical tests.
Host tests use synthetic ADC data. They do not prove device support.

## Build and start

Use GCC16 and a coherent EmberBSD application sysroot. Install these public headers:
`dev/usb/usbperiph.h`, `dev/fdt/adi_axi_adc.h`, and `dev/fdt/adi_axi_adc_stream.h`.
Keep host ABI inputs outside the target include and library searches.

```sh
make CC=/absolute/armv7--netbsdelf-eabihf-gcc \
  CPPFLAGS='--sysroot=/absolute/earmv7hf-sysroot' \
  LDFLAGS='--sysroot=/absolute/earmv7hf-sysroot -static'
./pluto-iiod --self-test
./pluto-iiod /dev/usbperiph0 /dev/adiadc0 YOUR_VID_HEX YOUR_PID_HEX
```

Select USB identifiers that you are authorized to use. There is no default VID/PID.
Do not use another vendor's identity. Select the resulting USB URI explicitly in libiio.
The interface string is `IIO`. Control endpoints are `0x81/0x01`.
Stream endpoints are `0x82/0x02`. Bulk max packet size is 512 bytes.
The product description contains `Pluto` for the SDR++ discovery filter.

The service applies an RX configuration before connecting USB. The initial settings
are 1090 MHz, 2.4 MSPS requested, 3 MHz RF bandwidth, and slow gain control.
It refuses startup if the driver cannot return an actual sample rate.
`--self-test` checks pthread startup without opening a device.
Startup and fatal errors report the operation and numeric error on stderr.
Thread errors use the pthread return code. USB event errors include their sequence and generation.
The service logs at most 32 successful events and their available read-only USB snapshots.
It also snapshots before and after connect. Error snapshots do not replace the original error.
Snapshot support is optional when building with older peripheral headers.
The first eight ADC read errors also report fresh stream status on the existing ADC descriptor.
Failed status queries report their own error and do not expose undefined ioctl outputs.
These observations retain the original read error and do not prove continuous sampling.

One session owns RX1. By default, four DMA blocks feed separately owned user buffers.
The USB driver copies each buffer again. ADC and USB never share DMA mappings.
Configuration changes stop and drain the stream before applying the next configuration.
They restart the stream only after successful configuration and clock readback.
Reset, suspend, and cable removal cancel the receive session.
Closing its owning pipe also cancels RX.
Failed stop or drain errors propagate. They do not permit a silent restart.

Each pipe has separate command and transfer admission locks. A short shared lock
protects RX ownership and pipe epochs. It is never held during ADC or USB waits.
A separate ADC transaction lock keeps one READBUF coherent across configuration changes.
Closing an unrelated pipe does not stop RX or wait for that READBUF.
A counted WRITE on another open pipe remains intact.

Close first disables admission in userland and closes both kernel endpoint gates.
The kernel closes each gate before cancel and drain. The service then waits for
old transfer calls and the closing pipe's command to finish before it acknowledges EP0.
Responses retain their original command epoch. They cannot acquire a new pipe epoch.
Global reset, suspend, disconnect, and shutdown close local admission before gating the current kernel generation.
A bus generation change during teardown therefore cannot turn successful cancellation into a stale-generation failure.
Vendor OPEN and CLOSE retain the exact setup generation and acknowledge only a successful matching transaction.
A gate or drain failure receives no successful close acknowledgement.
Startup tests gate support before CONNECT. Vendor OPEN reopens the selected pair.
Global RESET closes both pipes; a single CLOSE clears only that pipe's parser.

## Protocol and attributes

The protocol follows upstream [libiio v0.25 usb.c](https://github.com/analogdevicesinc/libiio/blob/v0.25/usb.c)
and [iiod-client.c](https://github.com/analogdevicesinc/libiio/blob/v0.25/iiod-client.c).
The implementation is new BSD-2-Clause code. It contains no copied libiio or Linux code.
Vendor interface requests 0, 1, and 2 reset, open, and close endpoint pairs.
Pipe identifiers are 0 and 1. Requests must have no payload and target interface zero.

`PRINT`, `VERSION`, `TIMEOUT`, `SET ... BUFFERS_COUNT`, `READ`, `WRITE`,
`OPEN`, `READBUF`, and `CLOSE` have bounded parsing. `ZPRINT` returns `EINVAL`
so libiio can fall back to plain XML. Cyclic buffers and TX buffers are rejected.
`TIMEOUT` sets the ADC poll deadline. `BUFFERS_COUNT` selects 3 through 16 blocks before OPEN.
Commands allow 511 bytes before LF. Attribute payloads allow 1024 bytes.
An oversized payload poisons its pipe until reset or closure. Its bytes cannot become commands.
A WRITE with an invalid length or too many tokens also poisons its pipe.
`READBUF` allows at most 4 MiB and sends chunks no larger than 16 KiB.
Only the exact I/Q mask `00000003` is accepted.
Protocol errors use Linux IIOD errno numbers, independently of the BSD host ABI.

| Device and channel | Attributes | Behavior |
| --- | --- | --- |
| `ad9361-phy`, input `voltage0` | `sampling_frequency`, `rf_bandwidth`, `rf_port_select`, `gain_control_mode`, `hardwaregain` | Native RX configuration; actual clock and gain readback |
| `ad9361-phy`, output `altvoltage0` | `frequency`, `powerdown` | RX LO; only powerdown zero is accepted |
| `ad9361-phy`, output `altvoltage1` | `powerdown` | TX safety policy; only one is accepted |
| `ad9361-phy` | `in_out_voltage_filter_fir_en`, `filter_fir_config` | FIR remains disabled; enable and configuration return errors |
| `cf-ad9361-lpc`, inputs `voltage0/1` | `sampling_frequency` | Actual RX sample clock |
| `cf-ad9361-lpc` | `sampling_frequency`, `rx_discontinuities`, `rx_dropped_blocks`, `rx_loss_unknown` | Native stream status |

TX powerdown describes the enforced receive-only policy. It is not a hardware measurement.
No TX sample channel, RF enable operation, or transmit stream is exposed.
RX gain modes are `manual`, `slow_attack`, and `fast_attack`.
RF ports are `A_BALANCED` and `B_BALANCED`. Unsupported operations return errors.
Frequency extends from 70 MHz through 6 GHz only where the receiver driver permits it.
AD9363 RF bandwidth remains limited to 20 MHz. Extended tuning is experimental.

The format is `le:s12/16>>0`: signed low 12 bits in little-endian 16-bit containers.
I precedes Q. Each complex sample contains four bytes.
Fresh status is checked after every ADC read. Known loss stops the stream and returns overflow.
Unknown dropped sample counts remain unknown; the service does not fabricate a loss-free result.

The no-FIR path rejects 1.536 MSPS. ADS-B requests 2.4 MSPS; actual qualified readback
is 2,399,999 Hz. Initial AIS requests 3.072 MSPS; actual readback is 3,071,999 Hz.
These are clock qualifications, not proof of decoded RF messages.
LocalRadar's existing AIS helper requests 1.536 MSPS and must fail until FIR support exists.
Its ADS-B backend sets RX rate directly and accepts a two-hertz readback tolerance.
SDR++ writes RX rate directly before calling `ad9361_set_bb_rate`.
The latter needs TX clock and FIR operations that this service rejects.
SDR++ ignores that helper's return value. Qualify its direct RX rate and displayed rate
on hardware before accepting a supported no-FIR mode. Low-rate FIR modes remain unsupported.

## Checks

```sh
CC=/absolute/host-gcc16 ABI_INCLUDE=/absolute/public-abi-include sh tests/run.sh
CC=/absolute/host-gcc16 ABI_INCLUDE=/absolute/public-abi-include \
  CFLAGS='-O1 -g -fsanitize=address,undefined' sh tests/run.sh
CC=/absolute/host-gcc16 ABI_INCLUDE=/absolute/public-abi-include \
  PYTHON=/absolute/python3 LIBIIO=/absolute/existing/libiio sh tests/run.sh
```

The ABI include directory must contain only the selected public `dev/` headers.
Do not add an entire kernel `sys/` tree to a host compiler's include search.
The test-only `sys/ioccom.h` bridge uses the host ioctl encoding.
Native builds always use the real EmberBSD header and ioctl values.

Tests exercise partial commands and writes, malformed input, allocation bounds,
channel masks, pipe ownership, actual-rate readback, rejected FIR and TX operations,
loss errors, USB descriptors, EP0 validation, and cleanup.
Real-worker syscall fixtures close during ADC waits, before responses, and during
transfer admission. They check no new response, bounded EP0 reply, independent pipe
closure, ignored ioctl error outputs, and no close acknowledgement after failed drain.
The optional test loads an existing libiio 0.25 library. It parses production XML,
sets and reads RX attributes, opens I/Q channels, and refills a synthetic buffer.
Set `LIBAD9361` to also check the existing helper's unsupported FIR error path.
Its loopback transport exercises IIOD protocol compatibility. It does not test USB.

Source and tests are AI-assisted by OpenAI Codex (GPT-6).
