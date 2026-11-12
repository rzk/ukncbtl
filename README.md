# ukncbtl
UKNCBTL — UKNC Back to Life! emulator, Windows x86-64 version.

### Portable build during the SDL migration

The portable CMake build currently contains the video decoder, screen scaler,
sound chip, snapshots, disassembler, disk-controller, motherboard, processor and memory-controller objects, and a
subset of the tests; it does **not** build the emulator yet. The legacy application still requires
the Windows toolchain documented below. The intended portable toolchain is
CMake, Ninja and a Clang toolchain with a C++23 standard library. On Windows,
Clang also needs a Windows SDK, compatible C++ standard library and linker
environment. The project does not yet claim a verified cross-platform release.

With the toolchain available on `PATH`, configure and run the portable tests:

```sh
cmake -S . -B build/portable -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DUKNCBTL_BUILD_LEGACY=OFF
cmake --build build/portable
ctest --test-dir build/portable --output-on-failure
```

On Windows set both `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER` to `clang-cl`
from an environment that provides the SDK, standard library and linker. Clang's
`clang` driver must also be on `PATH` to assemble the CPU overflow routines;
alternatively set `UKNCBTL_CLANG_ASSEMBLER` to its full path. GoogleTest is fetched at its
pinned commit; offline builds can use `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` as
described in [testing and performance](docs/performance.md). The portable
build defaults to enabled tests; `BUILD_TESTING=OFF` configures without test
targets. Benchmarks, Tracy and ASan remain legacy-only for now. The migration
checklist is in [docs/sdl-migration-baseline.md](docs/sdl-migration-baseline.md).

The overflow assembly has x86-64 and ARM64 implementations for Windows, Linux
and macOS. CMake selects a single target architecture; macOS universal builds
must currently be made as separate architecture builds. Windows x86-64 has
been executed locally; the other combinations still require native CI or hardware
verification even though their objects have been cross-assembled.

### Building on Windows

Install Visual Studio 2026 with the Desktop development with C++ workload and a
Windows SDK, and install LLVM/Clang with `clang` on `PATH` for the CPU assembly.
Use its bundled CMake (4.1 or newer for the Visual Studio 2026
generator), for example from a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

The executable is `build/Release/UKNCBTL.exe`. Use `--config Debug` for a
debug build, or open the CMake project in Visual Studio.
All C++ targets require C++23, including the renderer, tests and benchmarks.
CMake selects the compiler's corresponding standard mode (MSVC may spell this
`/std:c++latest`). Use a current MSVC toolset with `std::expected`, `std::print`
and C++23 ranges support. The checked-in historical VS2015 projects are not the
supported build entry point; configure and build through CMake.
The emulator requires the x64 target. CMake also supports older
installed Visual Studio generators; select the matching generator explicitly.
The build also produces `RenderVfw.dll` beside the executable; keep both files
together when distributing or moving the program. `cmake --install build
--config Release --prefix <destination>` installs both files. The default renderer
is included from the upstream renderer project under `third_party/RenderVfw`.
The ROM and other resources are embedded in the executable. This modern build
uses the current Microsoft toolchain and Windows SDK and does not target Windows
XP.

Run the CMake test suite after building the corresponding configuration:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The suite uses GoogleTest and CTest, including the existing core tests, exhaustive
disassembly and hint checks, and screenshot, WAV, and bitmap tests. Each legacy
core case is registered separately through a compatibility adapter, preserving
the Microsoft test sources and their abort-on-failure behavior. New cases can use
GoogleTest directly. Configure with `-DBUILD_TESTING=OFF` to omit the test targets.

GoogleTest, Google Benchmark and Tracy use pinned release commits fetched by
CMake only when their corresponding options are enabled. The first configure
needs Git and access to GitHub. See [testing and performance](docs/performance.md)
for offline builds, benchmarks and profiling.

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![Build status](https://ci.appveyor.com/api/projects/status/xicur65lusd5c3ab?svg=true)](https://ci.appveyor.com/project/nzeemin/ukncbtl)
[![CodeFactor](https://www.codefactor.io/repository/github/nzeemin/ukncbtl/badge)](https://www.codefactor.io/repository/github/nzeemin/ukncbtl)

![GitHub all releases](https://img.shields.io/github/downloads/nzeemin/ukncbtl/total)

![](https://github.com/nzeemin/ukncbtl/blob/master/docs/ukncbtl-keyboard.png)


### Эмулятор UKNCBTL

UKNCBTL — UKNC Back to Life! — это эмулятор компьютера Электроника МС-0511 (УКНЦ).
УКНЦ это советский домашний/учебный компьютер, построенный на двух процессорах КМ1801ВМ2, совместимых по системе команд с Электроника-60, ДВК и др.

Текущее состояние эмулятора: в разработке. Большая часть программного обеспечения запускается и работает.

Эмулируются:
 * Центральный и периферийный процессоры
 * Оба контроллера памяти
 * Видеоконтроллер
 * Контроллер гибких дисков (стандарт MZ, 400/800 КБ на диск)
 * Картриджи ПЗУ (24 КБ на картридж)
 * Звук
 * Контроллер жёсткого диска IDE (два варианта)
 * Работа с кассетным магнитофоном — эмулируется через работу с WAV PCM файлами
 * Последовательный порт (Стык С2)
 * Параллельный порт — через запись вывода в файл, для просмотра результата можно использовать утилиту [ESCParser](https://github.com/nzeemin/escparser)

Пока что НЕ эмулируются: сетевая карта, RAM-диски, джойстики.

Огромное спасибо всем тем, кто внёс свой вклад в создание и развитие эмулятора, особенно:
 * Феликс Лазарев — проделал огромную работу на начальном этапе развития эмулятора
 * Алексей Кислый (Alex_K) — оказал неоценимую помощь во всех вопросах, касающихся деталей работы УКНЦ

-----

### In English
*UKNCBTL* is cross-platform UKNC emulator for Windows/Linux/Mac OS X.
*UKNC* (*УКНЦ*, Электроника МС-0511) is soviet school computer based on two PDP-11 compatible processors KM1801VM2.

Current status: under development. Most of software works fine.

Emulated:
 * CPU and PPU
 * Both memory controllers
 * Video controller
 * FDD controller (MZ standard, 400K/800K .dsk/.trd files)
 * ROM cartridges (24K .bin files)
 * Sound
 * Hard disk (IDE, .img files) — can read/write and boot
 * Tape cassette — read/write WAV PCM files
 * Serial port (experimental)
 * Parallel port — just dump output to file, use ESCParser to visualize the result

NOT emulated yet: network card, RAM disks, joysticks.

-----

The UKNCBTL project consists of:
* [ukncbtl](https://github.com/nzeemin/ukncbtl/) the Windows x86-64 version.
* [ukncbtl-renders](https://github.com/nzeemin/ukncbtl-renders/) — renderers for UKNCBTL Win32.
* [ukncbtl-qt](https://github.com/nzeemin/ukncbtl-qt/) is based on Qt framework and works under Windows, Linux and Mac OS X.
* [ukncbtl-testbench](https://github.com/nzeemin/ukncbtl-testbench/) — test bench for regression and performance testing.
* [ukncbtl-utils](https://github.com/nzeemin/ukncbtl-utils/) — various utilities: rt11dsk, sav2wav, ESCParser etc.
* [ukncbtl-doc](https://github.com/nzeemin/ukncbtl-doc/) — documentation and screenshots.
* [ukncbtl-wasm](https://github.com/nzeemin/ukncbtl-wasm/) — web version of the emulator.
* Project wiki: https://github.com/nzeemin/ukncbtl-doc/wiki
  * Screenshots: https://github.com/nzeemin/ukncbtl-doc/wiki/Screenshots-ru
  * User's Manual (in Russian): https://github.com/nzeemin/ukncbtl-doc/wiki/Users-Manual-ru
