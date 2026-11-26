# Thread ownership and concurrency

The emulator core is single-thread owned at any given time. Both frontends hand
the board to a worker for each frame while their main thread prepares or presents
video, then wait for completion before accessing board state again. SDL copies
changed video RAM planes before handing off the board and decodes that immutable
copy while the frame worker advances it. With four or more reported logical
processors, SDL walks the display list in order and divides the independent
scanlines between the UI thread and a persistent video worker. The worker
starts its band while the UI finalizes ImGui draw data. Each thread decodes
and scales its band into a disjoint part of the reusable display buffer.
The split keeps each pair of rows used by interpolation together. Both
threads finish before presentation. SDL pumps native events if the video
worker takes longer than the short condition-variable wait. Each band writes
rows in display order so SDL can copy the whole image into a tightly packed
streaming texture, or copy in row order if the texture has padding. SDL
reuses the decoded pixels, scaled buffer and texture while RAM, the palette
and the scale mode are unchanged. A renderer reset recreates the texture from
the cached scaled buffer.
The displayed image reflects the state before that frame. A short handoff uses
the worker condition variable. When a frame or exclusive board operation takes
longer, SDL pumps native events and waits for a board completion event.
Ordinary events stay queued in order until the worker returns the board.
Timed checks cover missing wakes and allow progress when input is already
queued. During a slow frame the main thread can publish completed clipboard
results and collect finished screenshot, state, disk image and debugger
export writes using only the owned worker output. An exception from a frame
stops SDL emulation. Running or stepping
requires a reset or successful state load. The motherboard and its processors, memory, devices, callbacks and
attached media are not generally safe for concurrent access. In particular,
`SystemFrame` intentionally interleaves CPU and PPU execution with device ticks;
running the two processors independently would change guest-visible ordering.
Processor instruction tables also have an explicit process-wide `Init`/`Done`
lifetime, which must encompass every processor using them.

Between iterations, SDL waits for a native event or the next pacing timeout.
The usual event loop handles queued events in order, and the frame scheduler
still decides when the guest is due to advance. File and clipboard workers
wake SDL after publishing their results. One queued event can represent
several completions. The UI collects the results on its next iteration.
Timed polling also runs when the event queue cannot accept a wake.

Existing asynchronous boundaries are narrower:

* SDL reads startup settings on a worker while the main thread initializes the
  window, renderer and ImGui backends. The main thread applies the saved window
  size before showing the window on a fast startup. If settings are slow, SDL
  shows a loading title and pumps events. Media preparation runs on a worker
  while the UI installs its font, appearance and layout. Up to three
  independent ROM, disk, tape and state reads can be outstanding. The startup
  worker owns the board and installs the resulting media in request order.
  Aliases wait for earlier mounts and the corresponding file-sharing checks.
  The main thread collects the startup result before resetting the board or
  applying the validated state.
  Startup can observe a close request during either load. Canceling media
  loading stops new reads and installation and joins any reads already in
  progress. Other queued events remain available to the regular loop.
* On normal exit, SDL hands the idle board and pending file operations to a
  shutdown worker after taking the window and UI settings snapshot. SDL
  continues pumping window events and installs pending clipboard results.
  The worker drains outputs, discards unconsumed reads and saves settings
  after collecting any pending import. The UI reports errors after the join.
* The board's audio callback collects PCM during a frame. The frame worker
  hands completed PCM to the audio worker before the UI collects the frame,
  so audio submission can finish while video is being presented. The audio
  worker submits one owned batch at a time to SDL. If submission is still
  busy when another frame finishes, the new batch is dropped to bound
  latency. Startup, output selection and disconnect recovery open a paused
  SDL device on a separate worker while the guest can advance. The board
  worker installs the prepared stream between frames using the current speed,
  volume and run state. It detaches the prior callback and drains its pending
  submission before closing that stream. A disconnected device stops receiving
  PCM while its replacement opens. The output menu enumerates devices on a
  worker and reuses the list until a playback device is added or removed.
  Shutdown waits for an outstanding open without attaching it.
* SDL file-dialog completion can arrive on another thread. `sdl_file_dialog::State`
  protects the result and pending flag with a mutex; the callback retains shared
  ownership and wakes the window thread after publishing the result. Closing
  the window disables wakes from any dialog callback that arrives late.
* SDL captures the decoded frame for screenshots. Scaling, encoding and file
  replacement run on a worker using those owned pixels. At most one save is
  outstanding. The main thread reserves the
  destination until it collects the result, and shutdown waits for completion.
* Screenshot clipboard images are scaled and encoded on a worker. Screen
  text uses a separate worker to decode a captured display and its fonts.
  Emulation continues during both operations. The UI collects the newest
  pending request first and installs the result on the SDL thread. A
  successful copy supersedes older work. A failed copy leaves earlier
  requests eligible to publish. Shutdown collects both before SDL video exits.
* SDL state saves flush the floppy and capture the board image between frames
  on the frame worker. A separate writer receives the owned bytes. One state
  save can be outstanding, and its destination remains reserved until the UI
  collects the result. Shutdown waits for the writer and reports any
  unacknowledged failure.
* SDL state loads read and validate an owned image on a worker while emulation
  continues. After the floppy flush and audio pause, the frame worker
  applies the image and synchronizes the board's printer callback. Interactive
  resets also run on the frame worker. SDL pumps native events while the board
  is busy. The source path remains reserved until collection. Reset or the
  cancel command discards the pending result without interrupting the read.
  Quitting waits for any reader but does not apply its result.
* SDL disk-image creation builds and writes the image on a worker using a
  staged replacement. Its destination is reserved until the result is
  collected; shutdown waits for the write and reports unacknowledged failures.
* The frame worker captures owned data for debugger memory, display-list and
  console exports. A separate worker formats and writes it. Display-list
  formatting uses a video-memory snapshot. Destination reservations and
  shutdown cover the write. The frame worker captures the debugger memory view
  between frames, including the processor mapping and accessible addresses.
  A separate worker searches the owned bytes while the board can advance.
  SDL applies the result to the current search and discards it if the view or
  query changed, or the machine was reset or restored. The comparison
  baseline uses the same compact capture and samples registers at the same
  frame boundary.
* The SDL sprite viewer copies the selected CPU memory page and decodes it on
  a worker. One decode may be outstanding. The UI draws the latest completed
  image for the current selection, and uploads again when the page changes or
  the renderer loses its texture. The worker wakes SDL when the image is ready.
  Identical pages reuse the pixels and texture.
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
  Between frames, the frame worker runs the tape controls while SDL pumps
  native events. Stop, rewind and eject drain accepted samples before touching
  the stream.
  Playback reads and converts PCM in bulk one block ahead of the guest.
  Guest position tracks consumed samples. Seeking clears prefetched data,
  and an incomplete host read delivers complete samples before reporting
  the error.
* Interactive disk mounts prepare one image at a time. A worker opens the
  image and validates hard-disk geometry while emulation continues. The UI
  reserves the path through installation. Between frames, the frame worker
  rechecks slot occupancy, shared mounts and cartridge requirements, then
  installs the image. The same worker handles ejection and host-requested
  floppy flushes. SDL pumps native events during these operations and
  dispatches emulator commands after the worker returns the board. SDL-mounted
  hard disks can read one sector on a persistent worker during the emulated IDE
  seek. The board publishes data or failure at the original seek deadline.
  It compares the final sector address before accepting a speculative result.
  A reset makes an outstanding result unusable. A write waits for the reader
  before touching the stream at its original commit tick. Eject and shutdown
  join pending reads before closing the file.
  The SDL floppy controller prepares one predicted raw track and its markers
  while the guest uses the current track. It predicts the other side on an
  ordinary load and the adjacent cylinder after a step. The board uses a
  matching prepared track immediately and reads on demand after a miss or a
  failed prediction. A demand read or write waits for any outstanding read
  on the same stream. Reset invalidates the prediction, and flush, eject and
  shutdown coordinate with the worker before writing or closing the image.
* SDL printer output queues guest bytes in order and writes them on a
  persistent worker. One batch may be writing while another fills. A full
  queue waits for the writer. The frame worker hands off pending bytes at
  frame completion, and handles opening the printer stream and draining it
  on pause or stop. Shutdown drains it before releasing the board.
* SDL debugger trace records produced during a frame are collected into batches
  and written by a persistent worker. A batch is submitted at 256 KiB or at
  frame completion. One write can be outstanding, so a slow disk applies
  backpressure. The frame worker runs trace controls, steps and console commands
  between frames. Clear, stop and trace writes outside a frame wait for earlier
  batches with SDL pumping native events. The UI polls for write failures and
  hands failed trace shutdown back to the frame worker.

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
