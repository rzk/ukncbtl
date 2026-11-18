file(MAKE_DIRECTORY "${TEST_DIR}")
set(listing "${TEST_DIR}/worker-日本.lst")
string(ASCII 255 254 bom)
file(WRITE "${listing}" "${bom}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_SUBTITLE_PATH=${listing}" "UKNCBTL_TEST_SCREENSHOT_PATH=${listing}"
  UKNCBTL_TEST_SCREENSHOT_CONFLICT=1 "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL subtitle load failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_SUBTITLE_PATH=${listing}" UKNCBTL_TEST_SUBTITLE_CANCEL=1
  "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL subtitle cancellation failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_SUBTITLE_PATH=${TEST_DIR}/missing.lst" UKNCBTL_TEST_SUBTITLE_FAILURE=1
  "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL subtitle failure handling failed (${result}): ${error}")
endif()
file(REMOVE "${listing}")
