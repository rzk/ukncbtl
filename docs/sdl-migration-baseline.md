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

- Host serial-port and serial-backed network connections and their settings
  are removed. Keep the emulated machine's internal device behavior and tests.
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
