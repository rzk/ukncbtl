cmake_minimum_required(VERSION 3.20)
project(UKNCBTL LANGUAGES CXX)

# Use the same language contract for the application, renderer, tests and tools.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UKNCBTL_BUILD_LEGACY "Build the existing Windows/MSVC application during migration" OFF)
option(UKNCBTL_USE_PORTABLE_SETTINGS "Use the experimental portable settings backend in the legacy application" OFF)
option(UKNCBTL_ENABLE_SDL3 "Build the SDL3 migration targets" ON)
option(UKNCBTL_ENABLE_IMGUI "Build Dear ImGui SDL3 migration targets" ON)
if(UKNCBTL_ENABLE_IMGUI AND NOT UKNCBTL_ENABLE_SDL3)
  message(FATAL_ERROR "UKNCBTL_ENABLE_IMGUI requires UKNCBTL_ENABLE_SDL3=ON")
endif()
if(UKNCBTL_BUILD_LEGACY)
  if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The legacy application currently requires Windows and MSVC; set UKNCBTL_BUILD_LEGACY=OFF for portable targets")
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "The legacy application currently requires an x86-64 (x64) build")
  endif()
  enable_language(RC)
  # Match the legacy application's CRT and its dependencies.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
include(CTest)
option(UKNCBTL_BUILD_BENCHMARKS "Build headless Google Benchmark workloads" OFF)
option(UKNCBTL_BUILD_FRAME_BENCHMARK "Build portable frame, trace, and tape benchmarks" OFF)
option(UKNCBTL_ENABLE_TRACY "Instrument the emulator with Tracy" OFF)
option(UKNCBTL_ENABLE_ASAN "Instrument C++ targets and dependencies with AddressSanitizer" OFF)
if(NOT UKNCBTL_BUILD_LEGACY AND (UKNCBTL_BUILD_BENCHMARKS OR UKNCBTL_ENABLE_ASAN))
  message(FATAL_ERROR "Benchmarks and ASan are currently available only in the legacy build")
endif()
if(UKNCBTL_ENABLE_ASAN)
  # MSVC's runtime checks and edit-and-continue conflict with AddressSanitizer.
  string(REGEX REPLACE "/RTC[1su]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  # MSVC STL container annotations must agree in every linked object, including
  # GoogleTest, Benchmark and Tracy. Apply these before fetching dependencies.
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address;/Zi>")
  add_link_options(/INCREMENTAL:NO)
endif()
include(cmake/Dependencies.cmake)
include(cmake/CpuOverflow.cmake)

function(ukncbtl_toolchain target)
  target_compile_features(${target} PUBLIC cxx_std_23)
  if(MSVC)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/permissive-;/Zc:__cplusplus;/GS>")
  endif()
endfunction()

if(UKNCBTL_ENABLE_IMGUI)
  add_library(DearImGui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
  ukncbtl_toolchain(DearImGui)
  target_include_directories(DearImGui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
  target_link_libraries(DearImGui PUBLIC SDL3::SDL3)
  set(ui_font_data "${CMAKE_CURRENT_BINARY_DIR}/embedded_ui_font.inc")
  add_custom_command(OUTPUT "${ui_font_data}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${imgui_SOURCE_DIR}/misc/fonts/DroidSans.ttf" "-DOUTPUT=${ui_font_data}"
      -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFont.cmake"
    DEPENDS "${imgui_SOURCE_DIR}/misc/fonts/DroidSans.ttf" "${PROJECT_SOURCE_DIR}/cmake/EmbedFont.cmake" VERBATIM)
  add_library(UiFont STATIC emulator/UiFont.cpp "${ui_font_data}")
  ukncbtl_toolchain(UiFont)
  target_include_directories(UiFont PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(UiFont PRIVATE DearImGui)
  add_library(UiAppearance STATIC emulator/UiAppearance.cpp)
  ukncbtl_toolchain(UiAppearance)
  target_link_libraries(UiAppearance PUBLIC DearImGui)
  install(FILES "${PROJECT_SOURCE_DIR}/third_party/NOTICE-DroidSans"
    "${PROJECT_SOURCE_DIR}/third_party/LICENSE-Apache-2.0" DESTINATION licenses)
endif()

add_library(EmulatorProfiling INTERFACE)
if(UKNCBTL_ENABLE_TRACY)
  target_compile_definitions(EmulatorProfiling INTERFACE UKNCBTL_ENABLE_TRACY)
  target_link_libraries(EmulatorProfiling INTERFACE Tracy::TracyClient)
endif()

# The decoder depends only on the portable board interface and can be built
# before the rest of the emulator core has been migrated.
add_library(VideoDecoder STATIC emulator/VideoDecoder.cpp)
ukncbtl_toolchain(VideoDecoder)
target_include_directories(VideoDecoder PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(VideoDecoder PUBLIC EmulatorProfiling PRIVATE FrameWorker ScreenScale)

add_library(VideoPalette STATIC emulator/VideoPalette.cpp)
ukncbtl_toolchain(VideoPalette)

add_library(ScreenText STATIC emulator/ScreenText.cpp)
ukncbtl_toolchain(ScreenText)

add_library(ScreenTextCapture STATIC emulator/ScreenTextCapture.cpp)
ukncbtl_toolchain(ScreenTextCapture)
target_include_directories(ScreenTextCapture PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(ScreenTextCapture PRIVATE ScreenText VideoDecoder VideoPalette EmulatorCore)

add_library(ScreenScale STATIC emulator/util/ScreenScale.cpp)
ukncbtl_toolchain(ScreenScale)
target_include_directories(ScreenScale PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(ScreenScale PUBLIC EmulatorProfiling)

add_library(ScreenshotImage STATIC emulator/ScreenshotImage.cpp)
ukncbtl_toolchain(ScreenshotImage)
target_link_libraries(ScreenshotImage PRIVATE ScreenScale)

# Build the disassembler without the legacy precompiled header. Its hint
# functions resolve their memory-controller references when linked into core.
add_library(Disassembler OBJECT emulator/emubase/Disasm.cpp)
ukncbtl_toolchain(Disassembler)
target_include_directories(Disassembler PRIVATE "${PROJECT_SOURCE_DIR}/emulator")

add_library(MemoryController OBJECT emulator/emubase/Memory.cpp)
ukncbtl_toolchain(MemoryController)
target_include_directories(MemoryController PRIVATE "${PROJECT_SOURCE_DIR}/emulator")

add_library(Processor OBJECT emulator/emubase/Processor.cpp)
ukncbtl_toolchain(Processor)
target_include_directories(Processor PRIVATE "${PROJECT_SOURCE_DIR}/emulator")

add_library(Motherboard OBJECT emulator/emubase/Board.cpp)
ukncbtl_toolchain(Motherboard)
target_include_directories(Motherboard PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(Motherboard PRIVATE EmulatorProfiling)

add_library(HardDrive OBJECT emulator/emubase/Hard.cpp)
ukncbtl_toolchain(HardDrive)
target_include_directories(HardDrive PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(HardDrive PRIVATE EmulatorProfiling)

add_library(FloppyController OBJECT emulator/emubase/Floppy.cpp)
ukncbtl_toolchain(FloppyController)
target_include_directories(FloppyController PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(FloppyController PRIVATE EmulatorProfiling)

add_library(SoundAY STATIC emulator/emubase/SoundAY.cpp)
ukncbtl_toolchain(SoundAY)

add_library(EmulatorCore STATIC
  $<TARGET_OBJECTS:Motherboard> $<TARGET_OBJECTS:Disassembler>
  $<TARGET_OBJECTS:FloppyController> $<TARGET_OBJECTS:HardDrive>
  $<TARGET_OBJECTS:MemoryController> $<TARGET_OBJECTS:Processor>)
ukncbtl_toolchain(EmulatorCore)
target_link_libraries(EmulatorCore PUBLIC CpuOverflow SoundAY EmulatorProfiling PRIVATE FrameWorker)

add_library(MediaSlots STATIC emulator/MediaSlots.cpp)
ukncbtl_toolchain(MediaSlots)
target_include_directories(MediaSlots PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(MediaSlots PRIVATE EmulatorCore)

add_library(FrameControl STATIC emulator/FrameControl.cpp)
ukncbtl_toolchain(FrameControl)
target_include_directories(FrameControl PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(FrameControl PUBLIC EmulatorCore)

add_library(PreviewDebugger STATIC emulator/PreviewDebugger.cpp)
ukncbtl_toolchain(PreviewDebugger)
if(BUILD_TESTING)
  target_compile_definitions(PreviewDebugger PRIVATE UKNCBTL_TRACE_TEST_HOOKS)
endif()
target_include_directories(PreviewDebugger PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(PreviewDebugger PRIVATE FrameControl EmulatorCore SubtitleFile IniEncoding MemoryDump)

add_library(PreviewConsole STATIC emulator/PreviewConsole.cpp)
ukncbtl_toolchain(PreviewConsole)
target_include_directories(PreviewConsole PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(PreviewConsole PRIVATE PreviewDebugger EmulatorCore)

add_library(PreviewSprite STATIC emulator/PreviewSprite.cpp)
ukncbtl_toolchain(PreviewSprite)
target_include_directories(PreviewSprite PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(PreviewSprite PRIVATE EmulatorCore FrameWorker)

add_library(FrameTiming STATIC emulator/FrameTiming.cpp)
ukncbtl_toolchain(FrameTiming)

add_library(FramePacing STATIC emulator/FramePacing.cpp)
ukncbtl_toolchain(FramePacing)

add_library(HostInput STATIC emulator/Input.cpp)
ukncbtl_toolchain(HostInput)

add_library(WavPcmFile STATIC emulator/util/WavPcmFile.cpp)
ukncbtl_toolchain(WavPcmFile)

add_library(PreviewTape STATIC emulator/PreviewTape.cpp)
ukncbtl_toolchain(PreviewTape)
target_include_directories(PreviewTape PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(PreviewTape PRIVATE WavPcmFile EmulatorCore)
if(BUILD_TESTING)
  target_compile_definitions(PreviewTape PRIVATE UKNCBTL_TAPE_TEST_HOOKS)
endif()

add_library(DiskImageData STATIC emulator/DiskImageData.cpp)
ukncbtl_toolchain(DiskImageData)

add_library(StagedFile STATIC emulator/util/StagedFile.cpp)
ukncbtl_toolchain(StagedFile)

add_library(DiskImageFile STATIC emulator/DiskImageFile.cpp)
ukncbtl_toolchain(DiskImageFile)
target_link_libraries(DiskImageFile PUBLIC DiskImageData PRIVATE StagedFile)

add_library(PrinterOutput STATIC emulator/PrinterOutput.cpp)
ukncbtl_toolchain(PrinterOutput)

add_library(PreviewPrinter STATIC emulator/PreviewPrinter.cpp)
ukncbtl_toolchain(PreviewPrinter)
target_include_directories(PreviewPrinter PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(PreviewPrinter PRIVATE PrinterOutput EmulatorCore FrameWorker)
if(BUILD_TESTING)
  target_compile_definitions(PreviewPrinter PRIVATE UKNCBTL_PRINTER_TEST_HOOKS)
endif()

add_library(RomFile STATIC emulator/RomFile.cpp)
ukncbtl_toolchain(RomFile)

set(embedded_rom_input "${PROJECT_SOURCE_DIR}/emulator/res/uknc_rom.bin")
set(embedded_rom_data "${CMAKE_CURRENT_BINARY_DIR}/embedded_rom_data.inc")
add_custom_command(OUTPUT "${embedded_rom_data}"
  COMMAND "${CMAKE_COMMAND}" "-DINPUT=${embedded_rom_input}" "-DOUTPUT=${embedded_rom_data}"
    -P "${PROJECT_SOURCE_DIR}/cmake/EmbedRom.cmake"
  DEPENDS "${embedded_rom_input}" "${PROJECT_SOURCE_DIR}/cmake/EmbedRom.cmake"
  VERBATIM)
add_library(EmbeddedRom STATIC emulator/EmbeddedRom.cpp "${embedded_rom_data}")
ukncbtl_toolchain(EmbeddedRom)
target_include_directories(EmbeddedRom PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_library(PreviewRoms STATIC emulator/PreviewRoms.cpp)
ukncbtl_toolchain(PreviewRoms)
target_link_libraries(PreviewRoms PRIVATE RomFile EmbeddedRom EmulatorCore)

add_library(MemoryDump STATIC emulator/MemoryDump.cpp)
ukncbtl_toolchain(MemoryDump)

add_library(DisplayListDump STATIC emulator/DisplayListDump.cpp)
ukncbtl_toolchain(DisplayListDump)
target_include_directories(DisplayListDump PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(DisplayListDump PUBLIC VideoDecoder)

add_library(DebugLogFile STATIC emulator/util/DebugLogFile.cpp)
ukncbtl_toolchain(DebugLogFile)

find_package(Threads REQUIRED)
add_library(AsyncDiskMount STATIC emulator/AsyncDiskMount.cpp)
ukncbtl_toolchain(AsyncDiskMount)
target_include_directories(AsyncDiskMount PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(AsyncDiskMount PRIVATE MediaSlots EmulatorCore Threads::Threads)
target_link_libraries(PreviewTape PRIVATE Threads::Threads FrameWorker)
add_library(AsyncDiskImage STATIC emulator/AsyncDiskImage.cpp)
ukncbtl_toolchain(AsyncDiskImage)
target_link_libraries(AsyncDiskImage PRIVATE DiskImageFile Threads::Threads)
add_library(AsyncDebuggerExport STATIC emulator/AsyncDebuggerExport.cpp)
ukncbtl_toolchain(AsyncDebuggerExport)
target_link_libraries(AsyncDebuggerExport PRIVATE DisplayListDump MemoryDump Threads::Threads)
add_library(AsyncRoms STATIC emulator/AsyncRoms.cpp)
ukncbtl_toolchain(AsyncRoms)
target_link_libraries(AsyncRoms PRIVATE PreviewRoms Threads::Threads)
add_library(FrameWorker STATIC emulator/FrameWorker.cpp)
ukncbtl_toolchain(FrameWorker)
target_link_libraries(FrameWorker PUBLIC Threads::Threads)

if(UKNCBTL_BUILD_FRAME_BENCHMARK)
  add_executable(FrameOverlapBenchmark benchmarks/FrameOverlapBenchmark.cpp)
  ukncbtl_toolchain(FrameOverlapBenchmark)
  target_include_directories(FrameOverlapBenchmark PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
  target_link_libraries(FrameOverlapBenchmark PRIVATE FrameWorker VideoDecoder EmulatorCore ScreenScale)
endif()

add_library(BmpFile STATIC emulator/util/BmpFile.cpp)
ukncbtl_toolchain(BmpFile)

add_library(ImagePixels STATIC emulator/util/ImagePixels.cpp)
ukncbtl_toolchain(ImagePixels)

add_library(ScreenshotFormat STATIC emulator/util/ScreenshotFormat.cpp)
ukncbtl_toolchain(ScreenshotFormat)

if(UKNCBTL_ENABLE_SDL3)
  add_library(PngFile STATIC emulator/util/PngFile.cpp)
  ukncbtl_toolchain(PngFile)
  target_link_libraries(PngFile PRIVATE ImagePixels SDL3::SDL3)
  add_library(ScreenshotFile STATIC emulator/ScreenshotFile.cpp emulator/AsyncScreenshot.cpp)
  ukncbtl_toolchain(ScreenshotFile)
  target_link_libraries(ScreenshotFile PRIVATE Threads::Threads ScreenshotImage ScreenshotFormat BmpFile PngFile StagedFile)
  add_library(ScreenshotClipboard STATIC emulator/ScreenshotClipboard.cpp emulator/AsyncClipboard.cpp)
  ukncbtl_toolchain(ScreenshotClipboard)
  target_link_libraries(ScreenshotClipboard PRIVATE BmpFile ScreenshotImage SDL3::SDL3 Threads::Threads)
  if(BUILD_TESTING)
    target_compile_definitions(ScreenshotClipboard PRIVATE UKNCBTL_CLIPBOARD_TEST_HOOKS)
  endif()
  add_library(SdlScreenRenderer STATIC emulator/SdlScreenRenderer.cpp)
  ukncbtl_toolchain(SdlScreenRenderer)
  target_link_libraries(SdlScreenRenderer PUBLIC SDL3::SDL3 PRIVATE EmulatorProfiling)
  add_library(SdlKeyboard STATIC emulator/SdlKeyboard.cpp)
  ukncbtl_toolchain(SdlKeyboard)
  target_link_libraries(SdlKeyboard PUBLIC SDL3::SDL3)
  add_library(SdlDisplay STATIC emulator/SdlDisplay.cpp)
  ukncbtl_toolchain(SdlDisplay)
  target_link_libraries(SdlDisplay PUBLIC SDL3::SDL3 PRIVATE FrameWorker)
  add_library(SdlShortcuts STATIC emulator/SdlShortcuts.cpp)
  ukncbtl_toolchain(SdlShortcuts)
  target_link_libraries(SdlShortcuts PUBLIC SDL3::SDL3)
  add_library(SdlSprite STATIC emulator/SdlSprite.cpp)
  ukncbtl_toolchain(SdlSprite)
  target_link_libraries(SdlSprite PUBLIC SDL3::SDL3)
  add_library(SdlAudio STATIC emulator/SdlAudio.cpp)
  ukncbtl_toolchain(SdlAudio)
  target_include_directories(SdlAudio PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
  target_link_libraries(SdlAudio PRIVATE FrameWorker)
  target_link_libraries(SdlAudio PUBLIC SDL3::SDL3 PRIVATE EmulatorCore FramePacing EmulatorProfiling)
  add_library(SdlFileDialog STATIC emulator/SdlFileDialog.cpp)
  ukncbtl_toolchain(SdlFileDialog)
  target_link_libraries(SdlFileDialog PUBLIC SDL3::SDL3)
endif()

add_library(Subtitles STATIC emulator/Subtitles.cpp)
ukncbtl_toolchain(Subtitles)
target_include_directories(Subtitles PRIVATE "${PROJECT_SOURCE_DIR}/emulator")

add_library(IniDocument STATIC emulator/util/IniDocument.cpp)
ukncbtl_toolchain(IniDocument)

add_library(IniEncoding STATIC emulator/util/IniEncoding.cpp)
ukncbtl_toolchain(IniEncoding)
target_link_libraries(DebugLogFile PRIVATE IniEncoding)
target_link_libraries(PreviewDebugger PRIVATE DebugLogFile FrameWorker)

if(UKNCBTL_BUILD_FRAME_BENCHMARK)
  add_executable(TraceBatchBenchmark benchmarks/TraceBatchBenchmark.cpp)
  ukncbtl_toolchain(TraceBatchBenchmark)
  target_include_directories(TraceBatchBenchmark PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
  target_link_libraries(TraceBatchBenchmark PRIVATE PreviewDebugger DebugLogFile FrameWorker EmulatorCore)
  add_executable(TapeFrameBenchmark benchmarks/TapeFrameBenchmark.cpp)
  ukncbtl_toolchain(TapeFrameBenchmark)
  target_include_directories(TapeFrameBenchmark PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
  target_link_libraries(TapeFrameBenchmark PRIVATE PreviewTape WavPcmFile FrameWorker EmulatorCore)
endif()

add_library(SubtitleFile STATIC emulator/SubtitleFile.cpp)
ukncbtl_toolchain(SubtitleFile)
target_link_libraries(SubtitleFile PUBLIC Subtitles PRIVATE IniEncoding)
add_library(AsyncSubtitles STATIC emulator/AsyncSubtitles.cpp)
ukncbtl_toolchain(AsyncSubtitles)
target_link_libraries(AsyncSubtitles PRIVATE SubtitleFile Threads::Threads)

add_library(IniFile STATIC emulator/util/IniFile.cpp)
ukncbtl_toolchain(IniFile)
target_link_libraries(IniFile PUBLIC IniDocument IniEncoding)

add_library(MouseInput STATIC emulator/MouseInput.cpp)
ukncbtl_toolchain(MouseInput)

add_library(VirtualKeyboard STATIC emulator/VirtualKeyboard.cpp)
ukncbtl_toolchain(VirtualKeyboard)

add_library(PreviewOptions STATIC emulator/PreviewOptions.cpp)
ukncbtl_toolchain(PreviewOptions)
add_library(AutoBoot STATIC emulator/AutoBoot.cpp)
ukncbtl_toolchain(AutoBoot)

add_library(PreviewSettings STATIC emulator/PreviewSettings.cpp)
ukncbtl_toolchain(PreviewSettings)
target_include_directories(PreviewSettings PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(PreviewSettings PRIVATE IniFile ScreenScale FramePacing)
add_library(AsyncSettingsImport STATIC emulator/AsyncSettingsImport.cpp)
ukncbtl_toolchain(AsyncSettingsImport)
target_link_libraries(AsyncSettingsImport PRIVATE PreviewSettings Threads::Threads)

add_library(PreviewMediaRestore STATIC emulator/PreviewMediaRestore.cpp)
ukncbtl_toolchain(PreviewMediaRestore)
target_link_libraries(PreviewMediaRestore PRIVATE PreviewSettings PreviewRoms MediaSlots AsyncDiskMount PreviewTape EmulatorSnapshot Threads::Threads EmulatorProfiling)

add_library(SettingsStore STATIC emulator/util/SettingsStore.cpp)
ukncbtl_toolchain(SettingsStore)
target_link_libraries(SettingsStore PUBLIC IniFile)

add_executable(ukncbtl-import-ini tools/ImportIni.cpp)
ukncbtl_toolchain(ukncbtl-import-ini)
target_include_directories(ukncbtl-import-ini PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(ukncbtl-import-ini PRIVATE IniFile)

# The snapshot implementation uses the portable board interface; linking its
# operations into an executable also requires the emulator core.
add_library(EmulatorSnapshot STATIC emulator/Snapshot.cpp)
ukncbtl_toolchain(EmulatorSnapshot)
target_include_directories(EmulatorSnapshot PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(EmulatorSnapshot PUBLIC EmulatorCore PRIVATE StagedFile)

add_library(PreviewSnapshot STATIC emulator/PreviewSnapshot.cpp emulator/AsyncSnapshot.cpp)
ukncbtl_toolchain(PreviewSnapshot)
target_include_directories(PreviewSnapshot PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(PreviewSnapshot PRIVATE EmulatorSnapshot FrameTiming Threads::Threads)

if(UKNCBTL_ENABLE_IMGUI)
  add_executable(ukncbtl-sdl emulator/SdlMain.cpp)
  ukncbtl_toolchain(ukncbtl-sdl)
  if(BUILD_TESTING)
    target_compile_definitions(ukncbtl-sdl PRIVATE UKNCBTL_TEST_HOOKS UKNCBTL_TRACE_TEST_HOOKS)
  endif()
  target_include_directories(ukncbtl-sdl PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
  target_link_libraries(ukncbtl-sdl PRIVATE FrameWorker DearImGui UiFont UiAppearance SdlScreenRenderer SdlKeyboard SdlDisplay SdlShortcuts SdlSprite MouseInput VirtualKeyboard PreviewOptions AutoBoot PreviewDebugger PreviewConsole PreviewSprite DisplayListDump SdlAudio SdlFileDialog MediaSlots PreviewRoms PreviewSettings PreviewMediaRestore PreviewSnapshot PreviewPrinter PreviewTape DiskImageFile ScreenshotImage ScreenshotFile ScreenshotClipboard ScreenTextCapture VideoPalette FramePacing
    EmbeddedRom EmulatorCore VideoDecoder AsyncDiskImage AsyncDebuggerExport AsyncSubtitles AsyncRoms AsyncDiskMount AsyncSettingsImport)
  install(TARGETS ukncbtl-sdl RUNTIME DESTINATION bin)
  install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME UKNCBTL-LICENSE)
  install(FILES "${PROJECT_SOURCE_DIR}/third_party/NOTICE-RenderVfw" DESTINATION licenses)
  install(FILES "${sdl3_SOURCE_DIR}/LICENSE.txt" DESTINATION licenses RENAME SDL3-LICENSE)
  install(FILES "${imgui_SOURCE_DIR}/LICENSE.txt" DESTINATION licenses RENAME IMGUI-LICENSE)
  if(UKNCBTL_ENABLE_TRACY)
    install(FILES "${tracy_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME TRACY-LICENSE)
  endif()
endif()

if(BUILD_TESTING)
  include(GoogleTest)
  add_test(NAME IniImportCli COMMAND ${CMAKE_COMMAND}
    "-DIMPORTER=$<TARGET_FILE:ukncbtl-import-ini>"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/ini-import-cli-test"
    -P "${PROJECT_SOURCE_DIR}/tests/IniImportCli.cmake")
  function(ukncbtl_portable_test target)
    add_executable(${target} ${ARGN})
    ukncbtl_toolchain(${target})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${target} DISCOVERY_TIMEOUT 30)
  endfunction()
  ukncbtl_portable_test(BreakpointTests tests/BreakpointTests.cpp)
  ukncbtl_portable_test(SerializationTests tests/SerializationTests.cpp)
  ukncbtl_portable_test(InstructionHistoryTests tests/InstructionHistoryTests.cpp)
  ukncbtl_portable_test(RamChangeTests tests/RamChangeTests.cpp)
  ukncbtl_portable_test(CoreHeaderTests tests/CoreHeaderTests.cpp)
  ukncbtl_portable_test(VideoPaletteTests tests/VideoPaletteTests.cpp)
  target_link_libraries(VideoPaletteTests PRIVATE VideoPalette)
  ukncbtl_portable_test(ScreenTextPortableTests tests/ScreenTextPortableTests.cpp)
  target_link_libraries(ScreenTextPortableTests PRIVATE ScreenTextCapture ScreenText EmulatorCore Threads::Threads)
  ukncbtl_portable_test(ScreenshotFormatTests tests/ScreenshotFormatTests.cpp)
  target_link_libraries(ScreenshotFormatTests PRIVATE ScreenshotFormat)
  ukncbtl_portable_test(ScreenshotImageTests tests/ScreenshotImageTests.cpp)
  target_link_libraries(ScreenshotImageTests PRIVATE ScreenshotImage)
  ukncbtl_portable_test(MediaSlotsTests tests/MediaSlotsTests.cpp)
  target_compile_definitions(PreviewMediaRestore PRIVATE UKNCBTL_MEDIA_TEST_HOOKS)
  target_compile_definitions(MediaSlotsTests PRIVATE UKNCBTL_MEDIA_TEST_HOOKS)
  target_link_libraries(MediaSlotsTests PRIVATE MediaSlots PreviewMediaRestore PreviewRoms FrameWorker EmulatorCore AsyncDiskMount)
  ukncbtl_portable_test(MouseInputTests tests/MouseInputTests.cpp)
  target_link_libraries(MouseInputTests PRIVATE MouseInput)
  ukncbtl_portable_test(VirtualKeyboardTests tests/VirtualKeyboardTests.cpp)
  target_link_libraries(VirtualKeyboardTests PRIVATE VirtualKeyboard)
  ukncbtl_portable_test(PreviewOptionsTests tests/PreviewOptionsTests.cpp)
  target_link_libraries(PreviewOptionsTests PRIVATE PreviewOptions AutoBoot)
  ukncbtl_portable_test(PreviewDebuggerTests tests/PreviewDebuggerTests.cpp)
  target_compile_definitions(PreviewDebuggerTests PRIVATE UKNCBTL_TRACE_TEST_HOOKS)
  target_link_libraries(PreviewDebuggerTests PRIVATE PreviewDebugger FrameControl FrameWorker EmulatorCore)
  ukncbtl_portable_test(PreviewConsoleTests tests/PreviewConsoleTests.cpp)
  target_link_libraries(PreviewConsoleTests PRIVATE PreviewConsole PreviewDebugger EmulatorCore)
  ukncbtl_portable_test(PreviewSpriteTests tests/PreviewSpriteTests.cpp)
  target_link_libraries(PreviewSpriteTests PRIVATE PreviewSprite EmulatorCore)
  ukncbtl_portable_test(PreviewSettingsTests tests/PreviewSettingsTests.cpp)
  target_link_libraries(PreviewSettingsTests PRIVATE PreviewSettings IniFile AsyncSettingsImport)
  ukncbtl_portable_test(PreviewSnapshotTests tests/PreviewSnapshotTests.cpp)
  target_link_libraries(PreviewSnapshotTests PRIVATE PreviewSnapshot FrameWorker EmulatorCore)
  ukncbtl_portable_test(PreviewRomsTests tests/PreviewRomsTests.cpp)
  target_link_libraries(PreviewRomsTests PRIVATE PreviewRoms PreviewSnapshot EmbeddedRom EmulatorCore AsyncRoms FrameWorker)
  if(UKNCBTL_ENABLE_SDL3)
    ukncbtl_portable_test(Sdl3SmokeTests tests/Sdl3SmokeTests.cpp)
    target_link_libraries(Sdl3SmokeTests PRIVATE SDL3::SDL3)
    ukncbtl_portable_test(PngFileTests tests/PngFileTests.cpp)
    target_link_libraries(PngFileTests PRIVATE PngFile SDL3::SDL3)
    ukncbtl_portable_test(ScreenshotFileTests tests/ScreenshotFileTests.cpp)
    target_link_libraries(ScreenshotFileTests PRIVATE ScreenshotFile ScreenshotImage ScreenshotClipboard SDL3::SDL3)
    ukncbtl_portable_test(ScreenshotClipboardTests tests/ScreenshotClipboardTests.cpp)
    target_link_libraries(ScreenshotClipboardTests PRIVATE ScreenshotClipboard SdlDisplay SDL3::SDL3 Threads::Threads)
    ukncbtl_portable_test(SdlScreenRendererTests tests/SdlScreenRendererTests.cpp)
    target_link_libraries(SdlScreenRendererTests PRIVATE SdlScreenRenderer ScreenScale)
    ukncbtl_portable_test(SdlKeyboardTests tests/SdlKeyboardTests.cpp)
    target_link_libraries(SdlKeyboardTests PRIVATE SdlKeyboard HostInput VirtualKeyboard EmulatorCore)
    ukncbtl_portable_test(SdlDisplayTests tests/SdlDisplayTests.cpp)
    target_link_libraries(SdlDisplayTests PRIVATE SdlDisplay ScreenText FrameWorker SDL3::SDL3)
    ukncbtl_portable_test(SdlShortcutsTests tests/SdlShortcutsTests.cpp)
    target_link_libraries(SdlShortcutsTests PRIVATE SdlShortcuts)
    ukncbtl_portable_test(SdlSpriteTests tests/SdlSpriteTests.cpp)
    target_link_libraries(SdlSpriteTests PRIVATE SdlSprite SDL3::SDL3)
    ukncbtl_portable_test(SdlAudioTests tests/SdlAudioTests.cpp)
    target_link_libraries(SdlAudioTests PRIVATE SdlAudio FrameWorker ScreenshotFile EmulatorCore)
    ukncbtl_portable_test(SdlFileDialogTests tests/SdlFileDialogTests.cpp)
    target_link_libraries(SdlFileDialogTests PRIVATE SdlFileDialog)
  endif()
  if(UKNCBTL_ENABLE_IMGUI)
    add_executable(SdlStateFixture tests/SdlStateFixture.cpp)
    ukncbtl_toolchain(SdlStateFixture)
    target_include_directories(SdlStateFixture PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
    target_link_libraries(SdlStateFixture PRIVATE PreviewRoms EmulatorSnapshot EmulatorCore WavPcmFile SDL3::SDL3)
    ukncbtl_portable_test(ImGuiSdlTests tests/ImGuiSdlTests.cpp)
    target_link_libraries(ImGuiSdlTests PRIVATE DearImGui UiFont UiAppearance SdlScreenRenderer)
    add_test(NAME SdlFrontendSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlFrontendSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlFrameFailureSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_FRAME_FAILURE=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlFrameFailureSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlFrameAndPrinterFailureSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_FRAME_AND_PRINTER_FAILURE=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlFrameAndPrinterFailureSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlPrinterWorkerSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-printer-worker-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlPrinterWorker.cmake")
    set_tests_properties(SdlPrinterWorkerSmoke PROPERTIES TIMEOUT 45)
    add_test(NAME SdlSlowFrameSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_SLOW_FRAME=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlSlowFrameSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlAudioOverlapSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_AUDIO_OVERLAP=1 UKNCBTL_TEST_BOARD_OPERATIONS=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test --sound)
    add_test(NAME SdlAudioStartupOverlapSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_AUDIO_STARTUP_OVERLAP=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test --sound)
    add_test(NAME SdlAudioOpenExitSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_AUDIO_OVERLAP=1 UKNCBTL_TEST_BOARD_OPERATIONS=1 UKNCBTL_TEST_AUDIO_OPEN_EXIT=1
      $<TARGET_FILE:ukncbtl-sdl> --smoke-test --sound)
    set_tests_properties(SdlAudioOverlapSmoke SdlAudioStartupOverlapSmoke SdlAudioOpenExitSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlStaticVideoSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_STATIC_VIDEO=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlStaticVideoSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlSpriteWorkerSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_SPRITE_IMAGE=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlSpriteWorkerSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlScreenshotFailureExitSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_SCREENSHOT_FAILURE_EXIT=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlScreenshotFailureExitSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlSnapshotFailureExitSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_SNAPSHOT_FAILURE_EXIT=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlSnapshotFailureExitSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlScreenshotErrorMergeSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_SCREENSHOT_ERROR_MERGE=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlScreenshotErrorMergeSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlConcurrentScreenshotSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-screenshot-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlConcurrentScreenshot.cmake")
    set_tests_properties(SdlConcurrentScreenshotSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlConcurrentClipboardSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_COPY_SCREENSHOT=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test --sound)
    set_tests_properties(SdlConcurrentClipboardSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlClipboardDuringFrameSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_CLIPBOARD_DURING_FRAME=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlClipboardDuringFrameSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlClipboardSupersedeSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_COPY_SCREENSHOT=1 UKNCBTL_TEST_CLIPBOARD_SUPERSEDE=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    add_test(NAME SdlClipboardPendingExitSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_COPY_SCREENSHOT=1 UKNCBTL_TEST_CLIPBOARD_EXIT=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    add_test(NAME SdlImGuiClipboardSupersedeSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_COPY_SCREENSHOT=1 UKNCBTL_TEST_CLIPBOARD_SUPERSEDE=1 UKNCBTL_TEST_IMGUI_CLIPBOARD=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlClipboardSupersedeSmoke SdlClipboardPendingExitSmoke SdlImGuiClipboardSupersedeSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlScreenTextPendingExitSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_SCREEN_TEXT_EXIT=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlScreenTextPendingExitSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlDebuggerInspectionSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_DEBUGGER_INSPECTION=1 UKNCBTL_TEST_BOARD_OPERATIONS=1 $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    add_test(NAME SdlDebuggerSearchDiscardSmoke COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
      UKNCBTL_TEST_DEBUGGER_INSPECTION=1 UKNCBTL_TEST_DEBUGGER_SEARCH_DISCARD=1
      $<TARGET_FILE:ukncbtl-sdl> --smoke-test)
    set_tests_properties(SdlDebuggerInspectionSmoke SdlDebuggerSearchDiscardSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlConcurrentSnapshotSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-snapshot-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlConcurrentSnapshot.cmake")
    set_tests_properties(SdlConcurrentSnapshotSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlAsyncDiskImageSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-async-disk-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlAsyncDiskImage.cmake")
    set_tests_properties(SdlAsyncDiskImageSmoke PROPERTIES TIMEOUT 60)
    add_test(NAME SdlAsyncDebuggerExportSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-async-export-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlAsyncDebuggerExport.cmake")
    set_tests_properties(SdlAsyncDebuggerExportSmoke PROPERTIES TIMEOUT 120)
    add_test(NAME SdlAsyncSubtitlesSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-async-subtitles-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlAsyncSubtitles.cmake")
    set_tests_properties(SdlAsyncSubtitlesSmoke PROPERTIES TIMEOUT 90)
    add_test(NAME SdlAsyncRomsSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-async-roms-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlAsyncRoms.cmake")
    set_tests_properties(SdlAsyncRomsSmoke PROPERTIES TIMEOUT 120)
    add_test(NAME SdlAsyncTapeSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DFIXTURE=$<TARGET_FILE:SdlStateFixture>"
      "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-async-tape-test" -P "${PROJECT_SOURCE_DIR}/tests/SdlAsyncTape.cmake")
    set_tests_properties(SdlAsyncTapeSmoke PROPERTIES TIMEOUT 60)
    add_test(NAME SdlAsyncSettingsImportSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-async-settings-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlAsyncSettingsImport.cmake")
    set_tests_properties(SdlAsyncSettingsImportSmoke PROPERTIES TIMEOUT 60)
    add_test(NAME SdlConcurrentTraceSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-trace-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlConcurrentTrace.cmake")
    set_tests_properties(SdlConcurrentTraceSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlTraceControlsSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-trace-controls-test"
      -DTEST_TRACE_CONTROLS=ON -P "${PROJECT_SOURCE_DIR}/tests/SdlConcurrentTrace.cmake")
    set_tests_properties(SdlTraceControlsSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlTraceFailureRecoverySmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-trace-recovery-test"
      -DTEST_TRACE_FAILURE=ON -P "${PROJECT_SOURCE_DIR}/tests/SdlConcurrentTrace.cmake")
    set_tests_properties(SdlTraceFailureRecoverySmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlTraceFailureExitSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-trace-exit-test"
      -DTEST_TRACE_FAILURE_EXIT=ON -P "${PROJECT_SOURCE_DIR}/tests/SdlConcurrentTrace.cmake")
    set_tests_properties(SdlTraceFailureExitSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlTraceRecordFrameRecoverySmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-trace-frame-recovery-test"
      -DTEST_TRACE_RECORD_FAILURE=ON -P "${PROJECT_SOURCE_DIR}/tests/SdlConcurrentTrace.cmake")
    set_tests_properties(SdlTraceRecordFrameRecoverySmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlCombinedWorkersSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-combined-workers-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlCombinedWorkers.cmake")
    set_tests_properties(SdlCombinedWorkersSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlSnapshotWriteFailureSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-snapshot-error-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlSnapshotWriteFailure.cmake")
    set_tests_properties(SdlSnapshotWriteFailureSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlScreenshotMediaConflictSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-screenshot-conflict-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlScreenshotMediaConflict.cmake")
    set_tests_properties(SdlScreenshotMediaConflictSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlEarlyFrameExitSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-early-exit-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlEarlyFrameExit.cmake")
    set_tests_properties(SdlEarlyFrameExitSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlResponsiveShutdownSmoke COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-responsive-shutdown-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlResponsiveShutdown.cmake")
    set_tests_properties(SdlResponsiveShutdownSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME SdlStartupCli COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_FILE:ukncbtl-sdl>" "-DSTATE_FIXTURE=$<TARGET_FILE:SdlStateFixture>"
      "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-startup-test"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlStartupCli.cmake")
    set_tests_properties(SdlStartupCli PROPERTIES TIMEOUT 30)
    add_test(NAME SdlInstallSmoke COMMAND ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdl-install-test"
      "-DEXE_NAME=$<TARGET_FILE_NAME:ukncbtl-sdl>" "-DCONFIG=$<CONFIG>"
      -P "${PROJECT_SOURCE_DIR}/tests/SdlInstallSmoke.cmake")
    set_tests_properties(SdlInstallSmoke PROPERTIES TIMEOUT 60)
  endif()
  ukncbtl_portable_test(TraceTests tests/TraceTests.cpp)
  ukncbtl_portable_test(SettingsCodecTests tests/SettingsCodecTests.cpp)
  ukncbtl_portable_test(CachedSettingTests tests/CachedSettingTests.cpp)
  ukncbtl_portable_test(IniDocumentTests tests/IniDocumentTests.cpp)
  target_link_libraries(IniDocumentTests PRIVATE IniDocument)
  ukncbtl_portable_test(IniEncodingTests tests/IniEncodingTests.cpp)
  target_link_libraries(IniEncodingTests PRIVATE IniEncoding IniDocument)
  ukncbtl_portable_test(IniFileTests tests/IniFileTests.cpp)
  target_link_libraries(IniFileTests PRIVATE IniFile)
  ukncbtl_portable_test(SettingsStoreTests tests/SettingsStoreTests.cpp)
  target_link_libraries(SettingsStoreTests PRIVATE SettingsStore)
  ukncbtl_portable_test(StereoPcmBatchTests tests/StereoPcmBatchTests.cpp)
  ukncbtl_portable_test(FrameTimingTests tests/FrameTimingTests.cpp)
  target_link_libraries(FrameTimingTests PRIVATE FrameTiming)
  ukncbtl_portable_test(FramePacingTests tests/FramePacingTests.cpp)
  target_link_libraries(FramePacingTests PRIVATE FramePacing)
  ukncbtl_portable_test(InputTests tests/InputTests.cpp)
  target_link_libraries(InputTests PRIVATE HostInput)
  ukncbtl_portable_test(TapeTests tests/TapeTests.cpp)
  ukncbtl_portable_test(PreviewTapeTests tests/PreviewTapeTests.cpp)
  target_link_libraries(PreviewTapeTests PRIVATE PreviewTape WavPcmFile FrameWorker EmulatorCore)
  ukncbtl_portable_test(WavPcmFileTests tests/WavPcmFileTests.cpp)
  target_link_libraries(WavPcmFileTests PRIVATE WavPcmFile)
  ukncbtl_portable_test(DiskImageBuildTests tests/DiskImageBuildTests.cpp)
  target_link_libraries(DiskImageBuildTests PRIVATE DiskImageData)
  ukncbtl_portable_test(DiskImageFileTests tests/DiskImageFileTests.cpp)
  target_link_libraries(DiskImageFileTests PRIVATE DiskImageFile AsyncDiskImage)
  ukncbtl_portable_test(StagedFileTests tests/StagedFileTests.cpp)
  target_link_libraries(StagedFileTests PRIVATE StagedFile)
  ukncbtl_portable_test(PrinterOutputTests tests/PrinterOutputTests.cpp)
  target_link_libraries(PrinterOutputTests PRIVATE PrinterOutput)
  ukncbtl_portable_test(PreviewPrinterTests tests/PreviewPrinterTests.cpp)
  target_link_libraries(PreviewPrinterTests PRIVATE PreviewPrinter FrameWorker EmulatorCore)
  ukncbtl_portable_test(RomFileTests tests/RomFileTests.cpp)
  target_link_libraries(RomFileTests PRIVATE RomFile)
  configure_file("${embedded_rom_input}" "${CMAKE_CURRENT_BINARY_DIR}/embedded_rom_test.bin" COPYONLY)
  ukncbtl_portable_test(EmbeddedRomTests tests/EmbeddedRomTests.cpp)
  target_link_libraries(EmbeddedRomTests PRIVATE EmbeddedRom RomFile)
  ukncbtl_portable_test(MemoryDumpTests tests/MemoryDumpTests.cpp)
  target_link_libraries(MemoryDumpTests PRIVATE MemoryDump)
  ukncbtl_portable_test(DisplayListDumpTests tests/DisplayListDumpTests.cpp)
  target_link_libraries(DisplayListDumpTests PRIVATE AsyncDebuggerExport)
  target_link_libraries(DisplayListDumpTests PRIVATE DisplayListDump EmulatorCore)
  ukncbtl_portable_test(DebugLogFileTests tests/DebugLogFileTests.cpp)
  target_link_libraries(DebugLogFileTests PRIVATE DebugLogFile)
  ukncbtl_portable_test(BmpFileTests tests/BmpFileTests.cpp)
  target_link_libraries(BmpFileTests PRIVATE BmpFile)
  ukncbtl_portable_test(ImagePixelsTests tests/ImagePixelsTests.cpp)
  target_link_libraries(ImagePixelsTests PRIVATE ImagePixels)
  ukncbtl_portable_test(SubtitleTests tests/SubtitleTests.cpp)
  target_link_libraries(SubtitleTests PRIVATE Subtitles)
  ukncbtl_portable_test(SubtitleFileTests tests/SubtitleFileTests.cpp)
  target_link_libraries(SubtitleFileTests PRIVATE SubtitleFile IniEncoding AsyncSubtitles)
  ukncbtl_portable_test(HardPortableTests tests/HardPortableTests.cpp)
  target_compile_definitions(HardDrive PRIVATE UKNCBTL_HARD_PREFETCH_TEST_HOOKS)
  target_link_libraries(HardPortableTests PRIVATE FrameWorker EmulatorCore MediaSlots AsyncDiskMount)
  ukncbtl_portable_test(FloppyPortableTests tests/FloppyPortableTests.cpp $<TARGET_OBJECTS:FloppyController>)
  target_compile_definitions(FloppyController PRIVATE UKNCBTL_FLOPPY_PREFETCH_TEST_HOOKS)
  target_link_libraries(FloppyPortableTests PRIVATE FrameWorker EmulatorProfiling)
  ukncbtl_portable_test(PortableCoreTests tests/PortableCoreTests.cpp)
  target_link_libraries(PortableCoreTests PRIVATE FrameWorker FrameControl EmulatorSnapshot VideoDecoder)
  ukncbtl_portable_test(ScreenScaleTests tests/ScreenScaleTests.cpp)
  target_link_libraries(ScreenScaleTests PRIVATE ScreenScale)
  ukncbtl_portable_test(ScreenScaleReferenceTests tests/ScreenScaleReferenceTests.cpp)
  target_link_libraries(ScreenScaleReferenceTests PRIVATE ScreenScale)
  ukncbtl_portable_test(SoundAYEdgeTests tests/SoundAYEdgeTests.cpp)
  target_link_libraries(SoundAYEdgeTests PRIVATE SoundAY)
  ukncbtl_portable_test(CpuOverflowTests tests/CpuOverflowTests.cpp)
  target_link_libraries(CpuOverflowTests PRIVATE CpuOverflow)
endif()

if(NOT UKNCBTL_BUILD_LEGACY)
  return()
endif()

function(ukncbtl_target target)
  ukncbtl_toolchain(${target})
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX)
  target_precompile_headers(${target} PRIVATE "${PROJECT_SOURCE_DIR}/emulator/stdafx.h")
endfunction()

add_executable(UKNCBTL WIN32
  emulator/Common.cpp emulator/util/CommonText.cpp emulator/ConsoleView.cpp emulator/DebugView.cpp
  emulator/Dialogs.cpp emulator/DisasmView.cpp emulator/InstrHistoryView.cpp
  emulator/DisplayListView.cpp emulator/Emulator.cpp emulator/KeyboardView.cpp
  emulator/Main.cpp emulator/MainWindow.cpp emulator/MemoryView.cpp
  emulator/ScreenView.cpp emulator/ScreenText.cpp emulator/Settings.cpp emulator/SoundGen.cpp
  emulator/SpriteView.cpp emulator/ViewCalculations.cpp emulator/TapeView.cpp emulator/ToolWindow.cpp
  emulator/util/BitmapFile.cpp emulator/res/UKNCBTL.rc)
ukncbtl_target(UKNCBTL)
target_include_directories(UKNCBTL PRIVATE emulator/res)
target_link_libraries(UKNCBTL PRIVATE FrameWorker FrameControl FrameTiming FramePacing HostInput WavPcmFile EmulatorCore EmulatorSnapshot VideoDecoder ScreenScale vfw32 comctl32 winmm advapi32 shell32)
target_link_libraries(UKNCBTL PRIVATE DiskImageFile)
target_link_libraries(UKNCBTL PRIVATE PrinterOutput)
target_link_libraries(UKNCBTL PRIVATE RomFile)
target_link_libraries(UKNCBTL PRIVATE EmbeddedRom)
target_link_libraries(UKNCBTL PRIVATE MemoryDump)
target_link_libraries(UKNCBTL PRIVATE DisplayListDump)
target_link_libraries(UKNCBTL PRIVATE DebugLogFile)
target_link_libraries(UKNCBTL PRIVATE BmpFile)
target_link_libraries(UKNCBTL PRIVATE ScreenshotFormat)
target_link_libraries(UKNCBTL PRIVATE ScreenshotImage)
target_link_libraries(UKNCBTL PRIVATE VideoPalette)
target_link_libraries(UKNCBTL PRIVATE ImagePixels)
target_link_libraries(UKNCBTL PRIVATE SubtitleFile)
if(UKNCBTL_ENABLE_SDL3)
  target_compile_definitions(UKNCBTL PRIVATE UKNCBTL_SDL3_PNG)
  target_link_libraries(UKNCBTL PRIVATE PngFile ScreenshotFile)
else()
  target_link_libraries(UKNCBTL PRIVATE windowscodecs)
endif()
if(UKNCBTL_USE_PORTABLE_SETTINGS)
  target_compile_definitions(UKNCBTL PRIVATE UKNCBTL_PORTABLE_SETTINGS)
  target_link_libraries(UKNCBTL PRIVATE SettingsStore)
endif()
target_link_options(UKNCBTL PRIVATE "LINKER:/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'")

install(TARGETS UKNCBTL RUNTIME DESTINATION .)

if(BUILD_TESTING)
  include(GoogleTest)
  function(ukncbtl_test target)
    add_executable(${target} ${ARGN})
    ukncbtl_target(${target})
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${target} DISCOVERY_TIMEOUT 30)
  endfunction()

  ukncbtl_test(SnapshotTests tests/SnapshotTests.cpp)
  target_link_libraries(SnapshotTests PRIVATE EmulatorSnapshot EmulatorCore)
  ukncbtl_test(VideoDecoderTests tests/VideoDecoderTests.cpp)
  target_link_libraries(VideoDecoderTests PRIVATE VideoDecoder EmulatorCore)
  ukncbtl_test(CommandTests tests/CommandTests.cpp)
  target_link_libraries(CommandTests PRIVATE shell32)
  ukncbtl_test(ConsoleProductTests tests/ConsoleProductTests.cpp)
  target_compile_definitions(ConsoleProductTests PRIVATE PRODUCT)
  set_target_properties(ConsoleProductTests PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
  ukncbtl_test(SettingsTests tests/SettingsTests.cpp emulator/Settings.cpp)
  if(UKNCBTL_USE_PORTABLE_SETTINGS)
    target_compile_definitions(SettingsTests PRIVATE UKNCBTL_PORTABLE_SETTINGS)
    target_link_libraries(SettingsTests PRIVATE SettingsStore)
  endif()
  # Exercise the same application settings API against the new store without
  # changing the production backend before legacy INI migration is complete.
  add_executable(SettingsPortableTests tests/SettingsTests.cpp emulator/Settings.cpp)
  ukncbtl_target(SettingsPortableTests)
  target_compile_definitions(SettingsPortableTests PRIVATE UKNCBTL_PORTABLE_SETTINGS)
  target_link_libraries(SettingsPortableTests PRIVATE GTest::gtest_main SettingsStore)
  gtest_discover_tests(SettingsPortableTests TEST_PREFIX "PortableBackend." DISCOVERY_TIMEOUT 30)
  ukncbtl_test(IniWindowsCompatibilityTests tests/IniWindowsCompatibilityTests.cpp)
  target_link_libraries(IniWindowsCompatibilityTests PRIVATE IniDocument IniEncoding SettingsStore)
  ukncbtl_test(CommonTextTests tests/CommonTextTests.cpp emulator/util/CommonText.cpp)
  ukncbtl_test(SpriteTests tests/SpriteTests.cpp)
  ukncbtl_test(ViewCalculationTests tests/ViewCalculationTests.cpp emulator/ViewCalculations.cpp)
  ukncbtl_test(ScreenTextTests tests/ScreenTextTests.cpp emulator/ScreenText.cpp)
  ukncbtl_test(TextFormatTests tests/TextFormatTests.cpp)
  ukncbtl_test(BitmapFileTests tests/BitmapFileTests.cpp emulator/util/BitmapFile.cpp)
  target_include_directories(BitmapFileTests PRIVATE tests)
  target_link_libraries(BitmapFileTests PRIVATE BmpFile ImagePixels windowscodecs)
  if(UKNCBTL_ENABLE_SDL3)
    target_compile_definitions(BitmapFileTests PRIVATE UKNCBTL_SDL3_PNG)
    target_link_libraries(BitmapFileTests PRIVATE PngFile)
  endif()
  ukncbtl_test(BoardFrameTests tests/BoardFrameTests.cpp)
  target_link_libraries(BoardFrameTests PRIVATE EmulatorCore)
  ukncbtl_test(StorageControllerTests tests/StorageControllerTests.cpp)
  target_link_libraries(StorageControllerTests PRIVATE EmulatorCore)
  ukncbtl_test(MemoryEdgeTests tests/MemoryEdgeTests.cpp)
  target_link_libraries(MemoryEdgeTests PRIVATE EmulatorCore)
  ukncbtl_test(ProcessorEdgeTests tests/ProcessorEdgeTests.cpp)
  target_link_libraries(ProcessorEdgeTests PRIVATE EmulatorCore)
  ukncbtl_test(DiskImageWindowsTests tests/DiskImageWindowsTests.cpp)
  target_link_libraries(DiskImageWindowsTests PRIVATE DiskImageFile)
  ukncbtl_test(SoundOutputTests tests/SoundOutputTests.cpp emulator/SoundGen.cpp)
  target_link_libraries(SoundOutputTests PRIVATE FrameWorker winmm EmulatorProfiling)
  ukncbtl_test(CoreSmokeTests tests/CoreSmokeTests.cpp)
  target_link_libraries(CoreSmokeTests PRIVATE EmulatorCore)
  ukncbtl_test(DisasmCorpusTests tests/DisasmCorpusTests.cpp)
  target_link_libraries(DisasmCorpusTests PRIVATE EmulatorCore)
  ukncbtl_test(DisasmEdgeTests tests/DisasmEdgeTests.cpp)
  target_link_libraries(DisasmEdgeTests PRIVATE EmulatorCore)

  # Preserve the Microsoft test sources while registering each case with GoogleTest.
  # New tests can use native TEST/TEST_F/TEST_P macros directly.
  set(legacy_suites BoardTests DisasmTests FloppyTests HardDriveTests HintTests
      MemoryTests ProcessorControlTests ProcessorSystemTests ProcessorTests SoundAYTests)
  set(legacy_dir "${CMAKE_CURRENT_BINARY_DIR}/google-tests")
  file(MAKE_DIRECTORY "${legacy_dir}")
  set(legacy_wrappers)
  foreach(suite IN LISTS legacy_suites)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/tests/${suite}.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" contents)
    string(REGEX MATCHALL "TEST_METHOD\\([A-Za-z_][A-Za-z_0-9]*\\)" methods "${contents}")
    set(wrapper "#include \"stdafx.h\"\n#include \"${source}\"\n#include \"LegacyTestRunner.h\"\n")
    foreach(method IN LISTS methods)
      string(REGEX REPLACE "TEST_METHOD\\(([A-Za-z_][A-Za-z_0-9]*)\\)" "\\1" name "${method}")
      set(test_name "${name}")
      if(contents MATCHES "BEGIN_TEST_METHOD_ATTRIBUTE\\(${name}\\)[ \t\r\n]*TEST_IGNORE\\(\\)")
        set(test_name "DISABLED_${name}")
      endif()
      string(APPEND wrapper "TEST(${suite}, ${test_name}) { RunLegacyTest([] { ${suite} test; test.${name}(); }); }\n")
    endforeach()
    set(wrapper_path "${legacy_dir}/${suite}Runner.cpp")
    file(CONFIGURE OUTPUT "${wrapper_path}" CONTENT "${wrapper}" @ONLY)
    list(APPEND legacy_wrappers "${wrapper_path}")
  endforeach()
  add_executable(LegacyCoreTests tests/GoogleTestMain.cpp ${legacy_wrappers}
    tests/TestCommon.cpp tests/TestHelpers.cpp tests/BenchmarkWorkloadTests.cpp
    tests/GoogleTestAdapterTests.cpp)
  ukncbtl_target(LegacyCoreTests)
  target_include_directories(LegacyCoreTests PRIVATE tests/compat tests)
  target_link_libraries(LegacyCoreTests PRIVATE GTest::gtest EmulatorCore winmm)
  gtest_discover_tests(LegacyCoreTests DISCOVERY_TIMEOUT 30)
endif()

if(UKNCBTL_BUILD_BENCHMARKS)
  add_executable(EmulatorBenchmarks benchmarks/EmulatorBenchmarks.cpp)
  ukncbtl_target(EmulatorBenchmarks)
  target_link_libraries(EmulatorBenchmarks PRIVATE benchmark::benchmark EmulatorCore ScreenScale)
endif()
