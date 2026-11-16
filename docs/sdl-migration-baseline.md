# SDL3 migration: behavior baseline

This checklist describes the existing Windows application at commit `1efc02c`
and the intended SDL3/ImGui migration. It is an inventory from the source and
existing tests, not a claim that each manual scenario has been run. Leave items
unchecked until they have been verified in the new frontend on each supported
platform. The existing Windows frontend remains the reference during migration.

## Baseline build

The current supported build is Windows x64, MSVC and CMake; it requires
`RenderVfw.dll` beside the executable. From a Visual Studio developer shell:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

See [performance.md](performance.md) for offline dependencies, tests and
benchmarks. The commands above describe the baseline, not the intended future
toolchain. Record the build configuration and platform with every comparison.

The legacy application can optionally use the experimental portable settings
backend by configuring with `-DUKNCBTL_USE_PORTABLE_SETTINGS=ON`. The default
remains `OFF` while migration is in progress. The portable backend reads UTF-8
and BOM-marked UTF-16LE files; it does not recognize legacy Windows code-page
encodings. Use a separate UTF-8 or UTF-16LE settings file for this experiment:
an unmarked code-page file can be rejected or misinterpreted as UTF-8. Saves
replace the file and do not preserve its original filesystem metadata.
The portable file API can explicitly import Windows-1251 and Windows-1252
through `IniFile::ImportLegacy`, which creates a new UTF-8 file and refuses to
replace an existing destination. The destination filesystem must support hard
links. The `ukncbtl-import-ini` CMake target provides a command-line importer:

```text
ukncbtl-import-ini windows-1251 old.ini new.ini
```

Use `windows-1252` for that code page. The source code page must be known; it
is never detected automatically. On Windows, command-line paths are limited by
the process's narrow-character encoding; use ASCII paths if necessary. Import
is not yet exposed in the application UI.

Debug `trace.log` output is UTF-8 without a byte-order mark. Earlier Windows
builds wrote text using the system's active code page.

SDL3 migration targets can be enabled with `-DUKNCBTL_ENABLE_SDL3=ON`. CMake
fetches SDL 3.4.16 with a verified source hash; an offline checkout can be
provided with `FETCHCONTENT_SOURCE_DIR_SDL3`. The headless SDL smoke test checks
surface creation and PNG round trips without opening a window.
When enabled for the legacy application, this option routes PNG screenshot
saves through SDL and removes the application's Windows image codec dependency;
otherwise PNG saves still use Windows codecs.
The SDL screenshot clipboard adapter offers BMP and PNG data and requires an
initialized SDL video subsystem with event processing. It is not yet connected
to the legacy clipboard command. Its automated tests use SDL's dummy video
driver; system clipboard interoperability still needs platform testing.
The SDL screen renderer can draw prepared frames into an SDL renderer and is
tested with SDL's software renderer. It is not yet connected to a frontend or
the emulator's event loop; hardware rendering and platform display behavior
still need testing.
Dear ImGui and its SDL3 platform and SDL renderer backends can be built with
`-DUKNCBTL_ENABLE_SDL3=ON -DUKNCBTL_ENABLE_IMGUI=ON`. CMake pins Dear ImGui
v1.92.9b to a release commit; `FETCHCONTENT_SOURCE_DIR_IMGUI` supports an
offline checkout. A dummy-video test exercises UI rendering over a prepared
frame and SDL keyboard events. The experimental `ukncbtl-sdl` executable boots
the embedded ROM, displays the machine and provides pause and reset controls.
Physical keyboard positions use the legacy US-position mappings and follow the
emulated LAT/RUS mode; alternate physical layouts have not yet been validated.
It supports screenshot capture to PNG/BMP files or the SDL clipboard, with
the existing screenshot size modes. It also supports basic SDL audio output and
attaching/ejecting floppy and hard-disk images through SDL file dialogs. Disks
may be modified by the emulated machine; read-only mounts are indicated in the
menu. The Display menu offers the legacy RGB, GRB and grayscale palettes and
screen scaling modes; the preview fits each mode into a 4:3 viewport. Window
size, sound preference, display settings, and screenshot size are saved in the SDL
preference directory selected by `SDL_GetPrefPath("UKNCBTL", "SDL Preview")`,
separate from legacy settings.
Disk paths are not restored automatically. The preview preserves malformed
settings files without overwriting them; the smoke test does not read or write
preferences.
The SDL preview can save and load legacy-compatible `.uknc` state images. A
successful load pauses the machine; attached disk images remain attached and
are not stored in the state image. Interactive dialogs still need testing.
Audio output, file dialogs and disk interaction need interactive testing on each
platform. Linux dialogs may require an available desktop portal.
The `SdlFrontendSmoke` test runs three frames using SDL's dummy video driver;
interactive behavior and platform display compatibility still need testing.
Build the preview with `cmake --build <build-directory> --target ukncbtl-sdl`
after enabling both options above; the legacy frontend remains the supported
application during migration.

## Migration checklist

For each applicable item, check Windows, Linux and macOS separately before
declaring parity. Retain any required fixtures only when redistribution is
permitted; never commit user disk images, ROMs or recordings without checking.

### Machine and input

- [ ] Boot the embedded ROM and a representative bootable disk; reset, run,
      pause, autostart and exit cleanly.
- [ ] Exercise speeds 10%, 25%, 50%, 100%, 200%, 400% and maximum, with sound
      enabled and disabled. Check frame pacing and long-running stability.
- [ ] Preserve physical keyboard mapping, modifier and key-release behavior,
      on-screen keyboard, keyboard focus and shortcuts; test focus loss with a
      key held and typing into debugger controls.
- [ ] Enable and use the emulated mouse, including focus and button release.
- [ ] Preserve AY and Covox sound options, volume and audio output; check
      pause, resume, speed changes and an extended playback run.

### Display and capture

- [ ] Compare RGB, GRB and grayscale palettes, window resizing, aspect ratio and
      fullscreen. Compare frame pixels and capture representative screenshots.
- [ ] Preserve built-in equivalents of the bundled renderer's modes: 640x288
      standard; 640x432 1.5x; 640x576 scanlined and doubled; 960x576 scanlined;
      960x720 4:3; 1120x864 scanlined; and 1280x864 scanlined. Compare scaling
      and pixel rounding using the existing scaler tests and reference images.
- [ ] Save screenshots, copy an image to the clipboard and copy screen text;
      verify screenshot formats, dimensions and screen-to-text output. Confirm
      actual SDL clipboard image support per platform before claiming parity.
- [ ] Remove external renderer selection and loading. Map or ignore obsolete
      renderer settings and remove the renderer DLL and plugin ABI.

### Media and persistence

- [ ] Attach, read, write and detach floppy images in all four slots; create
      40- and 80-track images and handle read-only media and failures.
- [ ] Attach and detach both hard drives and ROM cartridges; check read/write
      and boot behavior with representative images.
- [ ] Save and load existing state files, preserving machine state and uptime;
      reject invalid files without corrupting running state.
- [ ] Read and write tape WAV PCM files and verify the tape controls.
- [ ] Preserve parallel-port output to a file and surface I/O failures.
- [ ] Preserve useful settings, command-line options, fonts and colors; import
      compatible old INI settings. Test Unicode paths and clean installation.
- [ ] Verify the embedded ROM and other required resources in installed builds.

### Debugger and tools

- [ ] Open the debugger, switch CPU/PPU, run to a breakpoint, step into and step
      over, and delete breakpoints.
- [ ] Compare register, stack, port, breakpoint, memory and disassembly views.
- [ ] Exercise instruction history, console and console log, display list,
      sprite viewer and disassembly subtitles.
- [ ] Preserve useful toolbar controls, status information and shortcuts.

## Intentional removals

- Host serial-port and serial-backed network connections and their host
  settings have been removed. Existing INI entries are left untouched. The
  emulated network station setting and internal device behavior remain.
- Renderer plugins and the bundled Windows renderer are removed; retained
  display modes become application-owned functionality.
- Application-owned WinAPI, MASM, Visual Studio projects and Visual Studio
  generator support are removed by the end of the migration. SDL's own native
  platform implementations are not application-owned WinAPI code.

## Reference material

- The menus and dialogs in `emulator/res/UKNCBTL.rc` inventory visible actions.
- `third_party/RenderVfw/RenderVfw.cpp` defines the bundled renderer modes.
- `tests/` and [unit-test-plan.md](unit-test-plan.md) describe existing automated
  checks. Automated tests do not replace platform-specific manual checks.
- `README.md` describes the existing media formats and emulator capabilities.
