# Thread ownership and concurrency

The emulator core is single-thread owned at any given time. Both frontends hand
the board to a worker for each frame while their main thread prepares or presents
video, then wait for completion before accessing board state again. SDL copies
the video RAM planes before handing off the board and decodes that immutable copy
while the worker advances it. The displayed image therefore reflects the state
before that frame. During a slow frame SDL
pumps native events on the main thread; emulator commands remain queued until
the worker returns the board. An exception from a frame stops SDL emulation;
running or stepping requires a reset or successful state load. The motherboard and its processors, memory, devices, callbacks and
attached media are not generally safe for concurrent access. In particular,
`SystemFrame` intentionally interleaves CPU and PPU execution with device ticks;
running the two processors independently would change guest-visible ordering.
Processor instruction tables also have an explicit process-wide `Init`/`Done`
lifetime, which must encompass every processor using them.

Existing asynchronous boundaries are narrower:

* SDL plays queued audio on its audio machinery. The board's sample callback
  writes the batch during a frame; the main thread submits or resets that batch
  only after the frame worker has finished. SDL owns consumption of the
  submitted audio stream.
* SDL file-dialog completion can arrive on another thread. `sdl_file_dialog::State`
  protects the result and pending flag with a mutex; the callback retains shared
  ownership and the main loop consumes the result.
* SDL screenshot scaling (when the capture mode differs from the display),
  encoding and file replacement run on a worker using owned pixels. At most one save is outstanding; the main thread reserves the
  destination until it collects the result, and shutdown waits for completion.
* Screenshot clipboard images are scaled and encoded on a worker while
  the UI and emulation advance. The UI installs the BMP and PNG after polling
  a completed result. A later successful text copy supersedes the
  pending image. Shutdown collects the copy before SDL video exits.
* SDL state saves capture the complete board image on the board-owning thread,
  then write the owned bytes on a worker. One state save can be outstanding; its
  destination remains reserved against media and other output until collected.
  Shutdown waits for the writer and reports any unacknowledged failure.
* SDL state loads read and validate an owned image on a worker while emulation
  continues. The UI applies a completed image between frames, after flushing
  floppy changes; the source path remains reserved until collection. Reset or
  the cancel command discards the pending result without interrupting the read.
  Quitting waits for any reader but does not apply its result.
* SDL disk-image creation builds and writes the image on a worker using a
  staged replacement. Its destination is reserved until the result is
  collected; shutdown waits for the write and reports unacknowledged failures.
* Debugger memory, display-list and console exports capture owned data on the
  board-owning thread and write it on a worker. Display-list formatting uses a
  video-memory snapshot; destination reservations and shutdown cover the write.
* Subtitle files are read and parsed on a worker. The debugger installs the
  owned result after a frame handoff; cancellation keeps the source reserved
  until the reader finishes, and a failed load leaves existing subtitles intact.
* System ROM and cartridge reads use a worker with owned image data. The UI
  rechecks the cartridge slot or flushes floppy changes before installing an
  image between frames; reset cancels a pending system ROM load. A pending
  source remains reserved until collection.
* Opening a WAV for playback or creating a recording runs on a worker. The UI
  installs the prepared stream between frames and reserves the path until
  collection. Shutdown finalizes a pending recording file without inserting
  it. Tape samples follow the guest tape clock. Recording queues up to two
  sample blocks for a persistent writer and waits when both buffers fill.
  Stop, rewind and eject drain accepted samples before touching the stream.
* Interactive disk mounts use one worker operation at a time. The worker opens
  the image and validates hard-disk geometry. The UI reserves the path and
  rechecks slot occupancy, shared mounts and cartridge requirements before
  installation. Floppy track flushing and preparation happen at the frame
  boundary; guest disk I/O follows the emulation schedule.
* SDL printer output queues guest bytes in order and writes them on a
  persistent worker. One batch may be writing while another fills. A full
  queue waits for the writer, and pause, stop and shutdown drain the output.
* SDL settings import captures current settings and flushes floppy changes
  before handing the read, decode and replacement to a worker. Shutdown
  collects an outstanding import before deciding whether to save the current
  session settings.
* SDL debugger trace records produced during a frame are collected into batches
  and written by a persistent worker for the active trace. A batch is submitted at 256 KiB or at frame
  completion; only one write is outstanding, so a slow disk applies backpressure
  instead of allowing unbounded buffering. Clear, stop, and trace writes outside
  a frame wait for earlier batches. Write errors are reported after detection.
* The legacy WinMM completion callback updates the atomic free-block count.
  Sample production runs during the worker-owned frame; control operations run
  on the main thread between frames. Slow audio playback can delay the handoff
  and therefore message processing.

The legacy frontend prepares input and completes status and error updates on the
UI thread. Tape callbacks detach immediately on end-of-file or I/O failure, but
defer control updates and errors until the frame returns. Debugger history,
trace output, printer and audio callbacks are used only during the frame; the UI
does not access their state until after the handoff.

Allowing the SDL UI to access the emulator while a frame is running requires a single owner for the entire live board,
an ordered command queue for input, debugger and media operations, and immutable
snapshots for video and debugger views. Start, stop, breakpoints, file errors,
audio callback detachment and shutdown need explicit acknowledgements and
lifetime handling. SDL window, renderer and UI operations must remain on the
appropriate frontend thread. These changes should preserve CPU/PPU tick ordering
and be validated against deterministic frame, device, debugger and snapshot
tests, with race detection on a supported toolchain. Measure actual frame and UI
latency with the profiling workflow in `performance.md` before deciding which
work to move; no speedup is implied by introducing a worker.
