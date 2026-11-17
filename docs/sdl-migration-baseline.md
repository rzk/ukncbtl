# SDL3 migration: behavior baseline

This checklist describes the existing Windows application at commit `1efc02c`
and the intended SDL3/ImGui migration. It is an inventory from the source and
existing tests, not a claim that each manual scenario has been run. Leave items
unchecked until they have been verified in the new frontend on each supported
platform. The existing Windows frontend remains the reference during migration.

## Baseline build

The historical reference build is Windows x64. It now uses the shared built-in
scaler without a renderer DLL. From a Windows developer shell with Clang and Ninja:

```powershell
cmake -S . -B build/legacy -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DUKNCBTL_BUILD_LEGACY=ON -DUKNCBTL_ENABLE_IMGUI=OFF -DUKNCBTL_ENABLE_SDL3=OFF
cmake --build build/legacy
ctest --test-dir build/legacy --output-on-failure
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
is available separately from the SDL frontend's settings import.

Debug `trace.log` output is UTF-8 without a byte-order mark. Earlier Windows
builds wrote text using the system's active code page.

SDL3 migration targets are enabled by default in fresh builds. CMake
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
The SDL screen renderer draws emulator frames in the SDL frontend and is also
tested with SDL's software renderer; hardware rendering and platform display
behavior still need interactive testing.
Dear ImGui and its SDL3 platform and SDL renderer backends can be built with
the default configuration. CMake pins Dear ImGui
v1.92.9b to a release commit; `FETCHCONTENT_SOURCE_DIR_IMGUI` supports an
offline checkout. A dummy-video test exercises UI rendering over a prepared
frame and SDL keyboard events. The experimental `ukncbtl-sdl` executable boots
the embedded ROM, displays the machine and provides pause and reset controls.
The Emulator menu offers a saved start-automatically preference and a reset
and boot action for ROM boot-menu selections 1 through 7. Boot inputs are timed
in emulated frames; resetting, pausing or losing focus cancels the sequence.
The executable accepts `--run`/`--paused`, `--sound`/`--no-sound`,
`--fullscreen`/`--windowed`, `--rom PATH`, `--floppy 0..3 PATH`,
`--cartridge 1..2 PATH`, `--hard-disk 1..2 PATH`, `--tape PATH`, and `--boot 1..7`.
Use `--floppy-read-only` or `--hard-disk-read-only` in place of the respective
disk option to prevent guest writes for that mount.
`--settings PATH` selects a separate settings file; its parent must exist.
Command-line overrides apply only to that launch. Media is attached before
the machine starts; a hard disk requires the corresponding cartridge. Use
`--help` for usage. Windows command-line arguments are received as UTF-8 via SDL.
Physical keyboard positions use the legacy US-position mappings and follow the
emulated LAT/RUS mode; alternate physical layouts have not yet been validated.
The Display menu also opens an on-screen UKNC keyboard. It supports Latin,
Cyrillic, bilingual or current-UKNC-mode legends and adjustable key sizes;
these preferences are saved. Shift and Ctrl latch for multi-key combinations.
The keyboard visibility is saved, and keys are released on focus loss, pause,
reset or window closure. Virtual and physical keys share the same input queue.
The Debug menu opens an initial debugger with CPU/PPU registers, disassembly,
memory inspection and per-processor octal breakpoints. It can pause, run, step,
and step over using the legacy core's breakpoint and debug-tick behavior. A
breakpoint pauses the machine and opens the debugger; stepping advances both
processors. Optional instruction/interrupt recording keeps separate recent
histories for CPU and PPU and is cleared on reset or state load. A display-list
inspector shows the decoded video tags. The remaining debugger tools are still
being migrated.
When paused, the debugger can capture a comparison baseline for both processors
and all memory views. Changed registers and accessible byte or word values are
highlighted until the baseline is cleared or recaptured. The baseline is kept
only for the current session and does not read device registers.
Memory inspection includes selected CPU/PPU mappings, raw RAM planes and ROM,
byte or word values, and octal or hexadecimal formatting. Inaccessible and I/O
locations are marked without reading devices; these view options are saved.
While paused, byte or word values can be written to raw RAM planes or writable
processor-mapped RAM using the selected octal or hexadecimal value format.
Memory search navigates forward or backward through the selected source, wraps
around the address space, and skips inaccessible locations without reading devices.
Memory exports use the chosen source and write inaccessible words as zero.
The disassembly view displays hints for the selected processor's current instruction.
Right-click debugger registers, stack, ports, memory cells or disassembly lines
to copy values and addresses in octal or hexadecimal. Disassembly and memory
context menus also navigate between views; instruction menus can manage
breakpoints or run to an address while paused.
The debugger can navigate disassembly by octal address, run to a temporary
target, inspect the stack and load UTF-16LE `.lst` subtitles. Subtitles mark
data words and comments; failed loads leave the current listing intact. The
preview embeds Droid Sans for Latin and Cyrillic text; its license is in
`third_party/LICENSE-Apache-2.0` with the accompanying font notice.
While paused, registers, PSW and aligned writable RAM words can be edited.
Memory ranges can be exported as little-endian binary and the display list as
text; output destinations currently used by the emulator are rejected.
The Debug menu also provides a console with command history and a bounded UTF-8
log that can be saved. It supports legacy-style register, memory, disassembly,
breakpoint, CPU/PPU selection and execution commands; enter `h` for its command
list. Register-addressed memory dumps, short disassembly and FIS floating-point
conversion are also available. The `u` and `udl` export commands open the
same save dialogs as the debugger controls. The Debug menu and `t` console
command can trace floppy activity and CPU/PPU instructions and interrupts to a
selected UTF-8 log. The Debug menu can select event types; `t` accepts legacy
octal masks and `tc` clears an active log. Tracing can slow emulation, and
write failures stop it.
Permanent CPU and PPU breakpoints, debugger/console visibility and the selected
processor are saved in the SDL preview preferences. Temporary step-over and
run-to targets are excluded. The Emulator menu also configures and saves the
emulated network station number.
The debugger also displays the legacy CPU and PPU port values. Its sprite
viewer decodes CPU memory in the five legacy color modes and supports octal
addresses, byte widths and page navigation; these controls are saved.
It supports screenshot capture to PNG/BMP files or the SDL clipboard, with
the existing screenshot size modes. It also supports basic SDL audio output and
attaching/ejecting floppy and hard-disk images through SDL file dialogs. Disks
may be modified by the emulated machine; read-only mounts are indicated in the
menu. The Disks menu can explicitly attach images read-only or read/write;
the requested mode is retained for restored media. Multiple mounts of the same
image are allowed only when all are read-only. The Disks menu can create 40-
or 80-track floppy images. The frontend
reports floppy read, write and unrepresentable-track failures, pauses
emulation and keeps dirty track changes in memory. The Disks menu can retry a
flush; reset, state save/load and exit check for pending writes, and a failed exit
offers retry, cancel, or an explicit exit anyway. The frontend also pauses on
hard-disk host read or write failures and identifies the affected drive. Failed
hard-disk sectors are not retained for retry. The Emulator menu can direct
parallel-port output to a selected file; Covox temporarily
suppresses printer output because it uses the same port. Creation and printer
output refuse files currently used by the emulator. Two ROM cartridge slots and an external system ROM can be selected; a
hard disk requires a cartridge in the matching slot. Changing the system ROM
resets the machine. A cartridge cannot be ejected while its matching hard disk
is attached; eject the disk first. The optional Emulator > Restore media on startup setting
remembers system ROM, cartridge, floppy and hard-disk paths. Failed restores
are reported and retained for the next launch, and automatic startup is paused
when a restore fails. Explicit run/boot options still take precedence.
Command-line media overrides apply only to the current launch. Disabling the
option clears the saved paths. The
Display menu offers the legacy RGB, GRB and grayscale palettes and
screen scaling modes; the preview fits each mode into a 4:3 viewport.
Display > Appearance provides dark, light and classic UI themes, a separate UI
size setting, and an option to reset tool-window placement on the next launch.
Tool-window positions and sizes are saved with the SDL preferences; ImGui keeps
windows reachable when the application window is resized.
Fullscreen can be toggled in the Display menu or with F10. Screen text can be
copied as UTF-8 using the UKNC character mapping from the Screenshot menu.
The optional toolbar provides run, reset, debugger and fullscreen controls; the
status bar shows running state, emulated uptime, processor and sound. Both can be
hidden in the Display menu. While paused, F8 steps and Shift+F8 steps over;
while running F8 remains available to the emulated keyboard. Media play/pause
and stop keys toggle execution and reset. Shortcuts are suppressed while ImGui
captures keyboard input or an SDL file dialog is pending.
Window size, sound preference, display settings, and screenshot size are saved in the SDL
preference directory selected by `SDL_GetPrefPath("UKNCBTL", "SDL Preview")`,
separate from legacy settings.
The active settings file, including an unreadable one, is protected from
being mounted as media or used as an output file by the frontend.
The preview also offers the legacy speed choices, volume, AY and Covox toggles.
Audio speed and pitch track the selected rate; maximum speed uses 500% audio as
in the legacy application. These settings are saved with the preview preferences.
Audio > Output selects the system default or an available playback device for
the current session. A failed device change leaves the existing output active;
after an audio failure, use that menu to retry without restarting the emulator.
The Emulator menu can enable the emulated mouse; its setting is saved with the
preview preferences. Pointer motion is scaled to the displayed screen, and mouse
input is released when the window loses focus, emulation pauses, or ImGui
captures the pointer. Short button presses are queued for emulated frames.
The Tape menu opens PCM WAV files or creates 44.1 kHz mono recordings, with
play, record, stop, rewind and eject controls. A tape panel shows position and
signal, and allows seeking in stopped playback. Playback stops at end of file,
and stopping a recording flushes a playable WAV while keeping the tape mounted
for resumption. I/O and finalization failures are reported. With media restoration enabled,
playback tapes reopen stopped; recording sessions are never restored automatically.
Mounted tape files are protected from other emulator output operations.
The preview preserves malformed
settings files without overwriting them; the smoke test does not read or write
preferences.
The Emulator menu can import compatible legacy preferences from an explicitly
selected UTF-8, UTF-16LE, Windows-1251 or Windows-1252 INI file. It imports
compatible debugger, display and sound settings and breakpoints, preserving
unrelated SDL preferences. An explicit import option also copies legacy floppy,
cartridge and hard-disk paths and enables restoration on the next launch.
Relative media paths are resolved against the legacy INI directory. The frontend
closes after saving; restart to apply the imported settings.
The SDL preview can save and load legacy-compatible `.uknc` state images. A
successful load pauses the machine; attached disks and cartridges remain
attached and are not stored in the state image. Interactive dialogs still need testing.
On Windows, dropping one file onto the SDL window opens a chooser for disk,
cartridge, ROM, tape, state or disassembly subtitles. Disks can be mounted
read-only, and occupied media slots must be ejected first. The chosen action
uses the same validation and persistence rules as the file dialogs.
Audio output, file dialogs and disk interaction need interactive testing on each
platform. Linux dialogs may require an available desktop portal.
The `SdlFrontendSmoke` test runs three frames using SDL's dummy video driver;
`SdlInstallSmoke` installs the frontend and licenses into a test directory and
checks that the installed executable starts with the dummy drivers. Interactive
behavior and platform display compatibility still need testing.
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
- [x] Remove external renderer selection and loading. Ignore obsolete renderer
      settings and remove the renderer DLL and plugin ABI.

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
- Renderer plugins and the renderer DLL are removed; display modes use the
  application-owned scaler in both frontends.
- The historical checked-in Visual Studio 2015 solution and project files have
  been removed, and the CPU overflow routines now use Clang-assembled source
  instead of MASM. Documented build and CI entry points use CMake and Ninja;
  application-owned WinAPI is removed by the end of the migration. SDL's own native platform
  implementations are not application-owned WinAPI code.

## Reference material

- The menus and dialogs in `emulator/res/UKNCBTL.rc` inventory visible actions.
- `third_party/RenderVfw/RenderVfw.cpp` defines the bundled renderer modes.
- `tests/` and [unit-test-plan.md](unit-test-plan.md) describe existing automated
  checks. Automated tests do not replace platform-specific manual checks.
- `README.md` describes the existing media formats and emulator capabilities.
