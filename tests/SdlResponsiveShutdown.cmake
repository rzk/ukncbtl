file(MAKE_DIRECTORY "${TEST_DIR}")
set(screenshot "${TEST_DIR}/shutdown.png")
file(REMOVE "${screenshot}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  UKNCBTL_TEST_SHUTDOWN_PUMP=1 "UKNCBTL_TEST_SCREENSHOT_PATH=${screenshot}"
  "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 20)
if(NOT result STREQUAL "0" OR NOT EXISTS "${screenshot}")
  message(FATAL_ERROR "SDL shutdown did not pump events and finish the screenshot (${result}): ${error}")
endif()
file(READ "${screenshot}" header OFFSET 0 LIMIT 24 HEX)
if(NOT header STREQUAL "89504e470d0a1a0a0000000d494844520000028000000240")
  message(FATAL_ERROR "Shutdown screenshot is not a 640x576 PNG: ${header}")
endif()
file(REMOVE "${screenshot}")
