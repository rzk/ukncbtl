# Correctness, benchmarks and profiling

Use GoogleTest for correctness, Google Benchmark for speed measurements, and
Tracy to locate bottlenecks. Benchmarks and ASan currently require the legacy
Windows x64/MSVC build; Tracy also supports the SDL frontend. Commands below run from the repository root in a Windows developer
shell with Ninja, the MSVC compiler and Clang available on `PATH`.

## Build options and dependencies

| CMake option | Default | Purpose |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | GoogleTest executables registered with CTest |
| `UKNCBTL_BUILD_BENCHMARKS` | `OFF` | Headless `EmulatorBenchmarks` executable |
| `UKNCBTL_ENABLE_TRACY` | `OFF` | Tracy instrumentation in the app and shared core/scaler |
| `UKNCBTL_ENABLE_ASAN` | `OFF` | MSVC AddressSanitizer for the application, renderer, tests and benchmarks |
| `UKNCBTL_BUILD_FRAME_BENCHMARK` | `OFF` | Portable, headless frame-overlap, trace, and tape measurements |

CMake fetches GoogleTest 1.17.0, Google Benchmark 1.9.4 and Tracy 0.12.2 by
pinned commit, as needed. GoogleTest and Benchmark are linked only into their
respective executables. With Tracy off, profiling macros compile away and no
Tracy dependency is downloaded or linked. Turning all three options off builds
the application without fetching any of these dependencies.

The legacy application and fetched libraries use the static MSVC runtime (`/MT`, or
`/MTd` for Debug). CMake's `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`,
`FETCHCONTENT_SOURCE_DIR_GOOGLEBENCHMARK` and `FETCHCONTENT_SOURCE_DIR_TRACY`
can point at local checkouts of the pinned revisions for offline builds.
Existing populated build directories can use `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON`.

## Correctness

```powershell
cmake -S . -B build/legacy -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release -DUKNCBTL_BUILD_LEGACY=ON -DUKNCBTL_ENABLE_IMGUI=OFF -DUKNCBTL_ENABLE_SDL3=OFF
cmake --build build/legacy --parallel
ctest --test-dir build/legacy --output-on-failure
```

Use `ctest --test-dir build/legacy -N` to list cases and `-R ProcessorTests`
to select a suite. `build/legacy/LegacyCoreTests.exe` also accepts native
GoogleTest options such as `--gtest_filter=ProcessorTests.*` and
`--gtest_output=xml:build/core-results.xml`.

The compatibility adapter reports legacy assertion locations, expected/actual
values and context through GoogleTest. Processor instruction tables are
initialized and released by a GoogleTest global environment, including when
CTest executes an individual case. The original standalone regression workloads
also run under GoogleTest; exhaustive corpus checks remain grouped to avoid
launching a process per opcode. Keep expected results independent of optimized
code, and preserve exact flags, memory effects, timing and pixel rounding.

## Memory-safety checks

For memory-safety checks, install the MSVC AddressSanitizer component and run
from Developer PowerShell so the sanitizer runtime DLL is on `PATH`:

```powershell
cmake -S . -B build/legacy-asan -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUKNCBTL_BUILD_LEGACY=ON -DUKNCBTL_ENABLE_IMGUI=OFF -DUKNCBTL_ENABLE_SDL3=OFF -DUKNCBTL_ENABLE_ASAN=ON
cmake --build build/legacy-asan
ctest --test-dir build/legacy-asan --output-on-failure
```

The sanitizer option disables incompatible MSVC runtime-check and incremental
linker settings. MSVC does not provide UBSan. Keep sanitizers off for performance
measurements.

## Speed measurements

To compare the SDL frame worker with serial video preparation without fetching a
benchmark library, enable the portable frame workload in a Release build:

```powershell
cmake -S . -B build/windows-clang -DUKNCBTL_BUILD_FRAME_BENCHMARK=ON
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
cmake -S . -B build/legacy -DUKNCBTL_BUILD_BENCHMARKS=ON -DUKNCBTL_ENABLE_TRACY=OFF
cmake --build build/legacy --target EmulatorBenchmarks --parallel
./build/legacy/EmulatorBenchmarks.exe --benchmark_min_time=0.5s --benchmark_repetitions=5 --benchmark_out=build/legacy/benchmark.json --benchmark_out_format=json
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
cmake -S . -B build/profile -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF -DUKNCBTL_ENABLE_TRACY=ON
cmake --build build/profile --target ukncbtl-sdl --parallel
./build/profile/ukncbtl-sdl.exe
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
`Floppy track read handoff` measures collection of floppy read-ahead work. The
legacy frontend also reports `Prepare screen` and `Audio buffer wait`.
Audio and pacing waits are deliberate
throttling, so inspect them separately from computation. The screen-scaling zone
appears only on paths that call the CPU scaler. No per-instruction or per-sample
zones are emitted, to keep the capture size and instrumentation cost manageable.

If needed, enable benchmarks in this profiling build to capture the synthetic
workloads too. They use the same instrumented core and scaler; their timings
should not be compared with the uninstrumented baseline.
