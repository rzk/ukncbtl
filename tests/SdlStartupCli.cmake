file(MAKE_DIRECTORY "${TEST_DIR}")
string(REPEAT "x" 24576 cartridge)
file(WRITE "${TEST_DIR}/cart-日本.rom" "${cartridge}")
string(ASCII 8 2 header)
string(REPEAT "x" 32766 body)
file(WRITE "${TEST_DIR}/disk-日本.img" "${header}${body}")
file(WRITE "${TEST_DIR}/floppy-日本.dsk" "${header}${body}")
execute_process(COMMAND "${STATE_FIXTURE}" "${TEST_DIR}/state-日本.uknc" RESULT_VARIABLE fixture_result)
if(NOT fixture_result EQUAL 0)
  message(FATAL_ERROR "Could not create startup state fixture: ${fixture_result}")
endif()

execute_process(COMMAND "${APP}" --help RESULT_VARIABLE result OUTPUT_VARIABLE output)
if(NOT result EQUAL 0 OR NOT output MATCHES "--boot")
  message(FATAL_ERROR "Help failed: ${result}: ${output}")
endif()
execute_process(COMMAND "${APP}" --boot 9 RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 2 OR NOT error MATCHES "--boot")
  message(FATAL_ERROR "Invalid boot was not rejected: ${result}: ${error}")
endif()
file(WRITE "${TEST_DIR}/settings.ini" "[SDLPreview]\nSound=0\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --settings "${TEST_DIR}/settings.ini" --floppy 0 "${TEST_DIR}/settings.ini"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 1 OR NOT error MATCHES "settings file")
  message(FATAL_ERROR "Settings file as media was not rejected: ${result}: ${error}")
endif()
file(READ "${TEST_DIR}/settings.ini" settings)
if(NOT settings STREQUAL "[SDLPreview]\nSound=0\n")
  message(FATAL_ERROR "Settings file was changed by the rejected media mount")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --settings "${TEST_DIR}/settings.ini" --state "${TEST_DIR}/settings.ini"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 1 OR NOT error MATCHES "settings file")
  message(FATAL_ERROR "Settings file as state was not rejected: ${result}: ${error}")
endif()
file(READ "${TEST_DIR}/settings.ini" settings)
if(NOT settings STREQUAL "[SDLPreview]\nSound=0\n")
  message(FATAL_ERROR "Settings file was changed by the rejected state load")
endif()
set(async_settings "${TEST_DIR}/settings-日本.ini")
file(WRITE "${async_settings}" "[SDLPreview]\nWindowWidth=1024\nWindowHeight=768\nUiScale=125\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  UKNCBTL_TEST_STARTUP_SETTINGS=1 "${APP}" --smoke-test --settings "${async_settings}"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 10)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Settings load did not pump events and apply the saved window size (${result}): ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  UKNCBTL_TEST_STARTUP_SETTINGS=1 UKNCBTL_TEST_STARTUP_SETTINGS_QUIT=1
  "${APP}" --smoke-test --settings "${async_settings}" --state "${TEST_DIR}/missing-state.uknc"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 10)
if(NOT result STREQUAL "0" OR error MATCHES "requested state")
  message(FATAL_ERROR "Quit during settings load did not stop media startup (${result}): ${error}")
endif()
file(REMOVE "${async_settings}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --smoke-test --hard-disk 1 "${TEST_DIR}/disk-日本.img"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 1)
  message(FATAL_ERROR "Hard disk without cartridge was not rejected: ${result}: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --smoke-test --tape "${TEST_DIR}/floppy-日本.dsk"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 1 OR NOT error MATCHES "requested tape")
  message(FATAL_ERROR "Invalid tape was not rejected: ${result}: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --smoke-test --state "${TEST_DIR}/floppy-日本.dsk"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 1 OR NOT error MATCHES "requested state")
  message(FATAL_ERROR "Invalid state was not rejected: ${result}: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  UKNCBTL_TEST_STARTUP_PUMP=1
  "${APP}" --smoke-test --state "${TEST_DIR}/state-日本.uknc" --run
  --cartridge 1 "${TEST_DIR}/cart-日本.rom" --hard-disk-read-only 1 "${TEST_DIR}/disk-日本.img"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Unicode state startup failed: ${result}: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --smoke-test --no-sound --boot 1 --floppy-read-only 3 "${TEST_DIR}/floppy-日本.dsk"
  --cartridge 1 "${TEST_DIR}/cart-日本.rom" --hard-disk-read-only 1 "${TEST_DIR}/disk-日本.img"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Unicode media startup failed: ${result}: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  UKNCBTL_TEST_STARTUP_PUMP=1 UKNCBTL_TEST_STARTUP_QUIT=1
  "${APP}" --smoke-test --cartridge 1 "${TEST_DIR}/cart-日本.rom"
  --hard-disk 1 "${TEST_DIR}/missing-startup.img"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0" OR error MATCHES "Could not attach requested hard disk")
  message(FATAL_ERROR "Quit while loading startup media failed (${result}): ${error}")
endif()
set(disk "${TEST_DIR}/disk-日本.img")
file(SHA256 "${disk}" original)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_HARD_MOUNT_PATH=${disk}" "UKNCBTL_TEST_SCREENSHOT_PATH=${disk}"
  UKNCBTL_TEST_SCREENSHOT_CONFLICT=1 "${APP}" --smoke-test --cartridge 1 "${TEST_DIR}/cart-日本.rom"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Async hard disk mount failed (${result}): ${error}")
endif()
file(SHA256 "${disk}" after)
if(NOT after STREQUAL original)
  message(FATAL_ERROR "Conflicting screenshot changed the hard disk image")
endif()
file(WRITE "${disk}" "invalid disk")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_HARD_MOUNT_PATH=${disk}" UKNCBTL_TEST_HARD_MOUNT_FAILURE=1
  "${APP}" --smoke-test --cartridge 1 "${TEST_DIR}/cart-日本.rom"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Async hard disk failure handling failed (${result}): ${error}")
endif()
set(floppy "${TEST_DIR}/floppy-日本.dsk")
file(SHA256 "${floppy}" original)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_FLOPPY_MOUNT_PATH=${floppy}" "UKNCBTL_TEST_SCREENSHOT_PATH=${floppy}"
  UKNCBTL_TEST_SCREENSHOT_CONFLICT=1 "${APP}" --smoke-test
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Async floppy mount failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_FLOPPY_MOUNT_PATH=${floppy}" UKNCBTL_TEST_BOARD_OPERATIONS=1 "${APP}" --smoke-test
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "SDL event pumping during disk install, eject and quit flush failed (${result}): ${error}")
endif()
file(SHA256 "${floppy}" after)
if(NOT after STREQUAL original)
  message(FATAL_ERROR "The floppy image changed during the read-only disk tests")
endif()
file(REMOVE "${TEST_DIR}/cart-日本.rom" "${TEST_DIR}/disk-日本.img" "${TEST_DIR}/floppy-日本.dsk" "${TEST_DIR}/settings.ini" "${TEST_DIR}/state-日本.uknc")
