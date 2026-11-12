cmake_minimum_required(VERSION 3.20)
project(UKNCBTL LANGUAGES CXX)

# Use the same language contract for the application, renderer, tests and tools.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UKNCBTL_BUILD_LEGACY "Build the existing Windows/MSVC application during migration" ${WIN32})
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
option(UKNCBTL_ENABLE_TRACY "Instrument the emulator with Tracy" OFF)
option(UKNCBTL_ENABLE_ASAN "Instrument C++ targets and dependencies with AddressSanitizer" OFF)
if(NOT UKNCBTL_BUILD_LEGACY AND (UKNCBTL_BUILD_BENCHMARKS OR UKNCBTL_ENABLE_TRACY OR UKNCBTL_ENABLE_ASAN))
  message(FATAL_ERROR "Benchmarks, Tracy and ASan are currently available only in the legacy build")
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
target_link_libraries(VideoDecoder PUBLIC EmulatorProfiling)

add_library(ScreenScale STATIC emulator/util/ScreenScale.cpp)
ukncbtl_toolchain(ScreenScale)
target_include_directories(ScreenScale PRIVATE "${PROJECT_SOURCE_DIR}/emulator")
target_link_libraries(ScreenScale PUBLIC EmulatorProfiling)

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

add_library(SoundAY STATIC emulator/emubase/SoundAY.cpp)
ukncbtl_toolchain(SoundAY)

# The snapshot implementation uses the portable board interface; linking its
# operations into an executable also requires the emulator core.
add_library(EmulatorSnapshot STATIC emulator/Snapshot.cpp)
ukncbtl_toolchain(EmulatorSnapshot)
target_include_directories(EmulatorSnapshot PRIVATE "${PROJECT_SOURCE_DIR}/emulator")

if(BUILD_TESTING)
  include(GoogleTest)
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
  ukncbtl_portable_test(TraceTests tests/TraceTests.cpp)
  ukncbtl_portable_test(ScreenScaleTests tests/ScreenScaleTests.cpp)
  target_link_libraries(ScreenScaleTests PRIVATE ScreenScale)
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

add_library(EmulatorCore STATIC
  emulator/emubase/Board.cpp $<TARGET_OBJECTS:Disassembler>
  emulator/emubase/Floppy.cpp emulator/emubase/Hard.cpp
  $<TARGET_OBJECTS:MemoryController> $<TARGET_OBJECTS:Processor>)
ukncbtl_target(EmulatorCore)
target_link_libraries(EmulatorCore PUBLIC CpuOverflow SoundAY)

target_link_libraries(EmulatorCore PUBLIC EmulatorProfiling)

add_executable(UKNCBTL WIN32
  emulator/Common.cpp emulator/util/CommonText.cpp emulator/ConsoleView.cpp emulator/DebugView.cpp
  emulator/Dialogs.cpp emulator/DiskImage.cpp emulator/DisasmView.cpp emulator/InstrHistoryView.cpp
  emulator/DisplayListView.cpp emulator/Emulator.cpp emulator/KeyboardView.cpp
  emulator/Main.cpp emulator/MainWindow.cpp emulator/MemoryView.cpp
  emulator/ScreenView.cpp emulator/ScreenText.cpp emulator/Subtitles.cpp emulator/Input.cpp emulator/Settings.cpp emulator/SoundGen.cpp
  emulator/SpriteView.cpp emulator/ViewCalculations.cpp emulator/TapeView.cpp emulator/ToolWindow.cpp
  emulator/util/BitmapFile.cpp emulator/util/WavPcmFile.cpp emulator/res/UKNCBTL.rc)
ukncbtl_target(UKNCBTL)
target_include_directories(UKNCBTL PRIVATE emulator/res)
target_link_libraries(UKNCBTL PRIVATE EmulatorCore EmulatorSnapshot VideoDecoder ScreenScale vfw32 comctl32 winmm advapi32 shell32 windowscodecs)
target_link_options(UKNCBTL PRIVATE "LINKER:/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'")

# The default renderer is loaded dynamically, so it must be built alongside the
# executable even when only the UKNCBTL target is requested.
add_library(RenderVfw SHARED third_party/RenderVfw/RenderVfw.cpp third_party/RenderVfw/RenderVfw.def)
ukncbtl_toolchain(RenderVfw)
target_compile_definitions(RenderVfw PRIVATE UNICODE _UNICODE)
target_link_libraries(RenderVfw PRIVATE vfw32)
add_dependencies(UKNCBTL RenderVfw)
install(TARGETS UKNCBTL RenderVfw RUNTIME DESTINATION .)

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
  ukncbtl_test(CommonTextTests tests/CommonTextTests.cpp emulator/util/CommonText.cpp)
  ukncbtl_test(SpriteTests tests/SpriteTests.cpp)
  ukncbtl_test(ViewCalculationTests tests/ViewCalculationTests.cpp emulator/ViewCalculations.cpp)
  ukncbtl_test(SubtitleTests tests/SubtitleTests.cpp emulator/Subtitles.cpp)
  ukncbtl_test(ScreenTextTests tests/ScreenTextTests.cpp emulator/ScreenText.cpp)
  ukncbtl_test(InputTests tests/InputTests.cpp emulator/Input.cpp)
  ukncbtl_test(TextFormatTests tests/TextFormatTests.cpp)
  ukncbtl_test(ImageScaleEdgeTests tests/ImageScaleEdgeTests.cpp third_party/RenderVfw/RenderVfw.cpp)
  target_link_libraries(ImageScaleEdgeTests PRIVATE ScreenScale vfw32)
  ukncbtl_test(BitmapFileTests tests/BitmapFileTests.cpp emulator/util/BitmapFile.cpp tests/BitmapTestResources.rc)
  target_include_directories(BitmapFileTests PRIVATE tests)
  target_link_libraries(BitmapFileTests PRIVATE windowscodecs)
  ukncbtl_test(BoardFrameTests tests/BoardFrameTests.cpp)
  target_link_libraries(BoardFrameTests PRIVATE EmulatorCore)
  ukncbtl_test(StorageControllerTests tests/StorageControllerTests.cpp)
  target_link_libraries(StorageControllerTests PRIVATE EmulatorCore)
  ukncbtl_test(MemoryEdgeTests tests/MemoryEdgeTests.cpp)
  target_link_libraries(MemoryEdgeTests PRIVATE EmulatorCore)
  ukncbtl_test(ProcessorEdgeTests tests/ProcessorEdgeTests.cpp)
  target_link_libraries(ProcessorEdgeTests PRIVATE EmulatorCore)
  ukncbtl_test(DiskImageTests tests/DiskImageTests.cpp emulator/DiskImage.cpp)
  ukncbtl_test(SoundOutputTests tests/SoundOutputTests.cpp emulator/SoundGen.cpp)
  target_link_libraries(SoundOutputTests PRIVATE winmm EmulatorProfiling)
  ukncbtl_test(TapeTests tests/TapeTests.cpp)
  ukncbtl_test(WavPcmFileTests tests/WavPcmFileTests.cpp emulator/util/WavPcmFile.cpp)
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
