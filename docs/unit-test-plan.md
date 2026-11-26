# Automated test coverage

Run the GoogleTest and SDL integration suites with the Windows Clang preset. See the [performance and testing guide](performance.md) for commands and test filters.

| Area | Coverage |
| --- | --- |
| CPU and board | Instruction semantics, signed overflow, addressing, memory maps, interrupts, timers, device callbacks, and serialization. |
| Storage and media | Drive and controller state, read-ahead and flush failures, image generation, WAV and tape, ROM loading, and state restoration. |
| Display and input | Video tags, scaling, screen text, palettes, sprites, virtual keyboard, SDL input, screenshots, and clipboard. |
| Debugger | Disassembly and hints, breakpoints, stepping, console commands and history, memory search, exports, and trace batches. |
| Settings | Unicode INI I/O, preferences and layout, media restore, filename aliases, and failed writes. |
| Application smoke tests | Startup, worker handoffs and failure recovery, shutdown, and launch from the install directory. |

Use the [desktop release checklist](desktop-release-checklist.md) for real audio devices, window behavior, file dialogs, drag-and-drop, and OS clipboard integration.
