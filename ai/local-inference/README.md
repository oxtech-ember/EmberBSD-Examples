# Local text and speech inference

Run llama.cpp and whisper.cpp on EmberBSD using local model files. The example
generates text, transcribes a WAV file, and tests a loopback HTTP completion.
It uses no cloud inference service or private project configuration.

Install the [Ports CPU packages](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/ai-cpu)
first. Validated engine versions are llama.cpp 0.6.0 and whisper.cpp 1.9.4.
Required utilities are POSIX shell tools, curl, a SHA256 utility, and `time`.
On NetBSD, the base tools provide everything except curl and the engines.

## Acquire models explicitly

```sh
sh fetch-assets.sh /var/tmp/ember-ai-models
```

The script downloads the three pinned assets in `assets.tsv`, checks their
sizes and SHA256 hashes, and only then gives each download its final name.
A verified cached file is reused without a request. A corrupt cached file
causes failure and is preserved for inspection. Interrupted downloads cannot
be mistaken for complete models. Downloads are not part of inference.

| Asset ID | File and size | Provenance and license |
| --- | --- | --- |
| `smollm2-135m` | SmolLM2-135M-Instruct Q4_K_M, 105,454,432 bytes | [bartowski GGUF conversion](https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF/tree/09816acd5d99df7be770d85ea30822623dab342c) of [HuggingFaceTB SmolLM2](https://huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct), Apache-2.0 |
| `whisper-tiny` | Multilingual Whisper tiny, 77,691,713 bytes | [whisper.cpp model conversion](https://huggingface.co/ggerganov/whisper.cpp/tree/5359861c739e955e79d9a303bcbc70fb988958b1), MIT |
| `jfk` | 11-second, 16-bit mono WAV, 352,078 bytes | [whisper.cpp sample](https://github.com/ggml-org/whisper.cpp/blob/927cfce34f31707e17f2bff35c349632fb9e2c3a/samples/jfk.wav), distributed in the upstream MIT repository |

Models and recordings are not stored in this repository or included in engine
packages. The small models are smoke-test fixtures, not a quality recommendation
for a production assistant. Only English transcription was exercised here.

Pass asset IDs after the directory to download a subset. For a different
model, use its own verified file directly, or supply `ASSET_MANIFEST` pointing
to a TSV with the same six columns. Keep its URL immutable and record its
license, byte count, and independently established SHA256.

## Run with local files

From a standard `/usr/pkg` installation:

```sh
AI_BIN=/usr/pkg/bin sh smoke.sh /var/tmp/ember-ai-models /var/tmp/ember-ai-result
```

For the isolated Ports installation, set `AI_BIN` to its `runtime/bin` directory.
Choose a new result directory. Default settings are two threads,
512 tokens of context, and port 18080 on `127.0.0.1`. Override `AI_THREADS` or
`AI_PORT` as needed. The script checks for a responding service before starting
its server, uses a unique model alias, and stops its own process on exit.
Startup polling and the HTTP request have time limits.

`AI_TEXT_MODEL`, `AI_SPEECH_MODEL`, and `AI_AUDIO` accept paths to alternative
local inputs. File transcription uses English (`-l en`); use `whisper-cli`
directly with another language when evaluating multilingual recognition.

Results include generated text, a transcript, HTTP JSON, engine logs, input
hashes, and environment details. `SUCCESS` is written only after all three
scenarios pass. Engine failures retain their nonzero status. On NetBSD,
`time -l` records elapsed time and maximum resident set size in KiB.
The runtime script does not call the downloader or contact a model catalog.

To use the engines directly:

```sh
llama-cli -m /path/to/model.gguf
whisper-cli -m /path/to/ggml-model.bin -f /path/to/recording.wav -l en -ng
llama-server -m /path/to/model.gguf --host 127.0.0.1 --port 18080 -ngl 0
```

No background service is installed. The example does not record audio or
connect hardware. Keep the HTTP endpoint on loopback unless you separately
configure access control for a network-facing deployment.

## Checks

```sh
sh tests/fetch-assets.sh
sh tests/smoke-failures.sh
sh tests/server-lifecycle.sh
```

These deterministic tests use local fixtures and do not download models.
They exercise verified reuse, corrupt and interrupted downloads, missing input,
engine failure, invalid port, startup timeout, and cancellation cleanup.
The real-model smoke run is the separate command above.

## Observed result

Tested on 2026-10-06 in an EmberBSD/NetBSD 11 AArch64 UTM VM: 4 virtual CPUs,
4 GiB RAM, two inference threads, CPU-only packages built with GCC 12.5.0.
The same packages passed install, remove, reinstall, and `pkg_admin` checks.

| Scenario | Observed result |
| --- | --- |
| SmolLM2 completion, at most 24 generated tokens | 0.45 s elapsed; 145,560 KiB peak RSS; generated a continuation naming Paris |
| Whisper tiny, upstream JFK sample | 3.40 s elapsed; 180,972 KiB peak RSS; recognized the "ask not" sentence |
| llama-server HTTP completion | Healthy loopback endpoint; returned text and 16 predicted tokens; server stopped after the test |

These are individual smoke measurements, not a controlled benchmark. Cache
state and other VM work affect timing. The guest network remained available;
the test consumed local files and used loopback for HTTP. This does not certify
a disconnected physical device, Russian transcription quality, GPU/NPU,
microphone input, image analysis, knowledge retrieval, or an agent integration.
