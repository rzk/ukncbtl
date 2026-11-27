# Emulator performance and testing

Run the commands from the repository root with CMake, Ninja, and Clang configured. The examples use a Windows developer command prompt. On Linux, use `linux-clang` in place of `windows-clang` in preset names and build paths, and omit `.exe` from executable names. Both platforms use the same SDL3 and Dear ImGui application build.

| Option | Purpose |
| --- | --- |
| BUILD_TESTING | Build and register the GoogleTest suites and SDL smoke tests. Enabled by default. |
| UKNCBTL_BUILD_FRAME_BENCHMARK | Build the frame, trace and tape comparisons. |
| UKNCBTL_BUILD_BENCHMARKS | Build the Google Benchmark CPU, frame and scaling workloads. |
| UKNCBTL_ENABLE_TRACY | Collect timing zones from the application or benchmarks. |
| UKNCBTL_ENABLE_ASAN | Instrument a developer build with AddressSanitizer. |
| UKNCBTL_ENABLE_UBSAN | Instrument a Linux developer build with UndefinedBehaviorSanitizer. |
| UKNCBTL_ENABLE_TSAN | Instrument a Linux developer build with ThreadSanitizer. |

## Tests and offline builds

```powershell
cmake --preset windows-clang
cmake --build --preset windows-clang --parallel
ctest --preset windows-clang
```

Run **ctest --test-dir build/windows-clang -N** to list cases. Use **ctest --preset windows-clang -R ProcessorTests** to filter by name. To use offline source checkouts, set `FETCHCONTENT_SOURCE_DIR_IMGUI` to the pinned Dear ImGui sources. On Windows, also set `FETCHCONTENT_SOURCE_DIR_SDL3` to the pinned SDL3 sources. Linux uses the installed SDL3 development package. Set `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`, `FETCHCONTENT_SOURCE_DIR_GOOGLEBENCHMARK`, and `FETCHCONTENT_SOURCE_DIR_TRACY` when enabling the corresponding tools.

## AddressSanitizer

```powershell
cmake --preset windows-clang -B build/asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUKNCBTL_ENABLE_ASAN=ON
cmake --build build/asan --parallel
ctest --test-dir build/asan --output-on-failure
```

Use the sanitizer build for local diagnostics. It uses the Clang sanitizer runtime and shared C/C++ runtime on Windows. Use the standard release build for distribution and performance measurements.

On Linux, add `-DUKNCBTL_ENABLE_UBSAN=ON` to the sanitizer configuration to match the combined checks run in CI.
For the frame worker race checks, configure a separate Linux build with `-DUKNCBTL_ENABLE_TSAN=ON`, build the
`PreviewDebuggerTests` target, and run CTest with `-R '^FrameWorker\.'`.

## Speed measurements

To compare the SDL frame worker with serial video preparation without fetching a
benchmark library, enable the portable frame workload in a Release build:

```powershell
cmake --preset windows-clang -DUKNCBTL_BUILD_FRAME_BENCHMARK=ON
cmake --build build/windows-clang --target FrameOverlapBenchmark --parallel
./build/windows-clang/FrameOverlapBenchmark.exe
```

This compares serial synthetic CPU/PPU execution and video decoding with two
overlapped variants. Both overlapped variants decode from a snapshot, and one
divides both decoding and scaling into bands. All three paths reuse the output
buffer. The benchmark rotates run order over five repetitions for the default
and 1120 × 864 interlaced modes, checks a checksum of sampled pixels, and
reports median frame and video preparation times. It excludes windowing,
presentation, audio, disk and pacing.

The benchmark also compares repeated decode and scaling of an unchanged board
with the RAM comparison used to reuse an existing image. It checks that the
cached pixels match the freshly rendered frame. This comparison measures CPU
preparation, excluding the SDL texture upload.

The upload layout comparison measures scaling and copying into a tightly
packed CPU buffer. It checks that a bottom-up scale with a reversed row copy
produces the same pixels as a top-down scale with a bulk copy. This comparison
excludes SDL and the graphics driver.

The same option also enables a trace comparison:

```powershell
cmake --build build/windows-clang --target TraceBatchBenchmark --parallel
./build/windows-clang/TraceBatchBenchmark.exe
```

It runs the same synthetic CPU/PPU workload with CPU instruction tracing, once
with a flush for every record and once with the batched writer. Each measurement
includes draining the log; three alternating repetitions are compared, and the
complete output files must match. Results depend on the host filesystem and do
not include SDL rendering, audio, or pacing.

For a focused tape recording comparison, build and run `TapeFrameBenchmark` with
the same option. It compares five 200-frame runs each with no tape,
direct 44.1 kHz recording and buffered recording. Timings include WAV
finalization, and the two recording modes must produce identical files at
the expected sample rate and length. It also compares direct playback against
read-ahead playback, checking the guest input bit and waveform every frame.
The benchmark excludes SDL rendering, audio output and pacing. Storage latency and caching can differ
substantially from a user's tape workload.

```powershell
cmake --preset windows-clang -B build/benchmarks -DUKNCBTL_BUILD_BENCHMARKS=ON -DUKNCBTL_ENABLE_TRACY=OFF
cmake --build build/benchmarks --target EmulatorBenchmarks --parallel
./build/benchmarks/EmulatorBenchmarks.exe --benchmark_min_time=0.5s --benchmark_repetitions=5 --benchmark_out=build/benchmarks/benchmark.json --benchmark_out_format=json
```

The benchmark executable has no GUI, ROM, disk image or GoogleTest dependency:

- `BM_CpuInstructions` runs batches of 1,024 guest instructions through the
  interpreter. It reports instructions/second and excludes cycle scheduling.
- `BM_SystemFrame` runs the real CPU/PPU scheduler using a synthetic RAM program,
  with host I/O disconnected. Each item is one emulated 20 ms frame. Timings are
  host execution time, not a promise of real-world application FPS.
- `BM_ScreenScale/0` through `/7` cover all scaling modes and report output
  pixels/second. Buffer allocation and initialization happen outside timing.

Both processors execute a deterministic immediate/register/memory/branch loop;
register values wrap naturally. GoogleTest verifies that the loop executes,
stores results and keeps both processors running across frames. Benchmark output
is consumed with optimization barriers. No timing assertions are added to CTest.
Run correctness tests before comparing performance, use the same machine and
build configuration, and measure with Tracy disabled. A quick smoke run uses
`--benchmark_min_time=0.01s`.

## Locate bottlenecks with Tracy

Use a separate directory to keep instrumented timings out of benchmark baselines:

```powershell
cmake --preset windows-clang -B build/profile -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF -DUKNCBTL_ENABLE_TRACY=ON
cmake --build build/profile --target ukncbtl --parallel
./build/profile/ukncbtl.exe
```

Open the Tracy **0.12.2** profiler from the [upstream release](https://github.com/wolfpld/tracy/releases/tag/v0.12.2)
and connect to `127.0.0.1` (default port 8086), then start emulation and exercise
the workload of interest. The client uses on-demand collection and listens only
on localhost. Capture collection begins when the profiler connects.

Frame markers delimit iterations of the host main loop, including paused
iterations. SDL zones cover the main loop, events, ImGui frame setup and interface, emulation,
video decode, screen scaling and drawing, audio submission, rendering, presentation,
and pacing. `Video memory snapshot` measures the RAM comparison and any copy
before the SDL frame worker starts. `ImGui draw data` overlaps the video band
worker on frames with new pixels. `Screen texture upload` appears when SDL
needs a new texture image. `Frame worker wait` measures the remaining wait
after video and presentation overlap the frame. Compare these zones and total
frame time when evaluating the worker. `Hard disk host read` measures the file
read on the read-ahead worker or on the board owner when a synchronous read
is needed. `Hard disk read handoff` measures the time to collect a pending
sector when the emulated seek ends, a write needs the stream, or a replacement
read reuses the worker. `Floppy host track read` measures host track reads, and
`Floppy track read handoff` measures collection of floppy read-ahead work.
Audio and pacing waits are deliberate
throttling, so inspect them separately from computation. The screen-scaling zone
appears only on paths that call the CPU scaler. No per-instruction or per-sample
zones are emitted, to keep the capture size and instrumentation cost manageable.

If needed, enable benchmarks in this profiling build to capture the synthetic
workloads too. They use the same instrumented core and scaler; their timings
should not be compared with the uninstrumented baseline.
