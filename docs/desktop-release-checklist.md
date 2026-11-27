# Desktop release checklist

Run these checks on Windows using the installed SDL3 and Dear ImGui application. Use a clean Windows VM to start the package without developer tools or third-party runtime installs. Run the CTest suite separately. Dummy video and audio tests cover startup and automated behavior.

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

### Media and persistence

- [ ] Attach, read, write and detach floppy images in all four slots; create
      40- and 80-track images and handle read-only media and failures.
- [ ] Attach and detach both hard drives and ROM cartridges; check read/write
      and boot behavior with representative images.
- [ ] Save and load existing state files, preserving machine state and uptime;
      reject invalid files without corrupting running state.
- [ ] Read and write tape WAV PCM files and verify the tape controls.
- [ ] Preserve parallel-port output to a file and surface I/O failures.
- [ ] Reopen to verify settings and tool layout. Exercise startup options, Unicode
      paths, and an upgrade from an existing SDL settings file.
- [ ] Verify the embedded ROM and other required resources in installed builds.

### Debugger and tools

- [ ] Open the debugger, switch CPU/PPU, run to a breakpoint, step into and step
      over, and delete breakpoints.
- [ ] Inspect register, stack, port, breakpoint, memory and disassembly views.
- [ ] Exercise instruction history, console and console log, display list,
      sprite viewer and disassembly subtitles.
- [ ] Preserve useful toolbar controls, status information and shortcuts.

The [test guide](unit-test-plan.md) identifies automated coverage. Repeat the desktop checks on Linux and macOS when validating those hosts.
