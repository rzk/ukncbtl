# UKNCBTL

UKNCBTL (UKNC Back to Life!) is a UKNC emulator with an SDL3 and Dear ImGui interface.

## Build and install

The build uses CMake 3.20 or newer, Ninja, and Clang with a C++23 standard library. CMake fetches pinned SDL3, Dear ImGui, and test dependency sources at configure time. See the [performance and testing guide](docs/performance.md) for offline builds and optional developer tools.

### Linux

Put Clang, CMake, Ninja, Git, and `pkg-config` on PATH. With libstdc++, use Clang 19 or newer and libstdc++ 13 or newer for `std::expected` and `std::format`. Install the SDL development dependencies for your desktop backends. If SDL reports a missing optional X11 extension, its configure error names the option you can disable. [SDL Linux build guide](https://wiki.libsdl.org/SDL3/README-linux)

On Ubuntu 24.04, install the build tools and SDL desktop development libraries.

```sh
sudo apt-get update
sudo apt-get install clang-19 libstdc++-13-dev cmake ninja-build git pkg-config \
  libx11-dev libxext-dev libxcursor-dev libxfixes-dev libxi-dev libxrandr-dev \
  libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libdecor-0-dev \
  libgl-dev libegl-dev libasound2-dev libpulse-dev libdbus-1-dev
```

Build and install from the repository root. Use `cmake --preset linux-clang` for the first command if `clang` and `clang++` on PATH already meet the requirements above.

```sh
cmake --preset linux-clang -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19
cmake --build --preset linux-clang
ctest --preset linux-clang
cmake --install build/linux-clang --prefix build/install-linux
./build/install-linux/bin/ukncbtl
```

Use `linux-clang-debug` for a Debug build or configure with `-DBUILD_TESTING=OFF` for an application-only build. The install includes the emulator and license notices.

### Windows

Open an x64 developer command prompt with the Windows SDK, C++ standard library, and linker configured. Put CMake, Ninja, and Clang on PATH. The presets use `clang-cl` for C and C++.

```powershell
cmake --preset windows-clang
cmake --build --preset windows-clang
ctest --preset windows-clang
cmake --install build/windows-clang --prefix build/install
```

Run **build/install/bin/ukncbtl.exe**. The ROM, interface font, SDL3, Dear ImGui, and C/C++ runtime are built into the standard Windows release. The install also includes the applicable license notices.

Use `windows-clang-debug` for a Debug build or configure with `-DBUILD_TESTING=OFF` for an application-only build.

### Running UKNCBTL

Use **ukncbtl --help** for startup options or **--settings path-to-ini** to choose a settings file in an existing directory. Existing SDL preferences and saved tool layout are picked up automatically. To migrate settings from the retired Win32 frontend, use a previous SDL build that still offers **Import legacy settings and exit**, then open the updated application.

The [desktop release checklist](docs/desktop-release-checklist.md) covers native display, audio, dialogs, clipboard, and other behavior beyond the dummy-driver smoke tests.

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
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
* [ukncbtl](https://github.com/nzeemin/ukncbtl/) the SDL3 and Dear ImGui version.
* [ukncbtl-renders](https://github.com/nzeemin/ukncbtl-renders/) — renderers for UKNCBTL Win32.
* [ukncbtl-qt](https://github.com/nzeemin/ukncbtl-qt/) is based on Qt framework and works under Windows, Linux and Mac OS X.
* [ukncbtl-testbench](https://github.com/nzeemin/ukncbtl-testbench/) — test bench for regression and performance testing.
* [ukncbtl-utils](https://github.com/nzeemin/ukncbtl-utils/) — various utilities: rt11dsk, sav2wav, ESCParser etc.
* [ukncbtl-doc](https://github.com/nzeemin/ukncbtl-doc/) — documentation and screenshots.
* [ukncbtl-wasm](https://github.com/nzeemin/ukncbtl-wasm/) — web version of the emulator.
* Project wiki: https://github.com/nzeemin/ukncbtl-doc/wiki
  * Screenshots: https://github.com/nzeemin/ukncbtl-doc/wiki/Screenshots-ru
  * User's Manual (in Russian): https://github.com/nzeemin/ukncbtl-doc/wiki/Users-Manual-ru
