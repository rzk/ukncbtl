file(MAKE_DIRECTORY "${TEST_DIR}")
set(disk "${TEST_DIR}/mounted.dsk")
string(REPEAT "x" 5120 disk_contents)
file(WRITE "${disk}" "${disk_contents}")
file(SHA256 "${disk}" before)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  UKNCBTL_TEST_SCREENSHOT_CONFLICT=1 "UKNCBTL_TEST_SCREENSHOT_PATH=${disk}"
  "${APP}" --smoke-test --floppy-read-only 0 "${disk}"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL screenshot conflict smoke test failed (${result}): ${error}")
endif()
file(SHA256 "${disk}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "Mounted disk was modified by the screenshot request")
endif()
file(REMOVE "${disk}")
