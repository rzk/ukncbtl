file(MAKE_DIRECTORY "${TEST_DIR}")
set(screenshot "${TEST_DIR}/early-exit.png")
file(REMOVE "${screenshot}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  UKNCBTL_TEST_SLOW_FRAME=1 UKNCBTL_TEST_EARLY_FRAME_EXIT=1
  "UKNCBTL_TEST_SCREENSHOT_PATH=${screenshot}" "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 1)
  message(FATAL_ERROR "Early frame exit returned ${result}, expected 1: ${error}")
endif()
if(NOT EXISTS "${screenshot}")
  message(FATAL_ERROR "Outstanding screenshot was not finished during cleanup")
endif()
file(READ "${screenshot}" header OFFSET 0 LIMIT 24 HEX)
if(NOT header MATCHES "^89504e470d0a1a0a0000000d494844520000028000000240")
  message(FATAL_ERROR "Early-exit screenshot is not a 640x576 PNG: ${header}")
endif()
file(REMOVE "${screenshot}")
