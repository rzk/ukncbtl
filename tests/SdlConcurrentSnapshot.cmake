file(MAKE_DIRECTORY "${TEST_DIR}")
set(snapshot "${TEST_DIR}/worker-日本.uknc")
file(REMOVE "${snapshot}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_SNAPSHOT_PATH=${snapshot}" UKNCBTL_TEST_BOARD_OPERATIONS=1
  UKNCBTL_TEST_SAVE_DURING_FRAME=1 "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL concurrent state-save smoke test failed (${result}): ${error}")
endif()
if(NOT EXISTS "${snapshot}")
  message(FATAL_ERROR "State image was not saved before shutdown")
endif()
file(SIZE "${snapshot}" size)
file(READ "${snapshot}" header OFFSET 0 LIMIT 12 HEX)
if(NOT size EQUAL 229888 OR NOT header STREQUAL "554b4e4342544c2101000100")
  message(FATAL_ERROR "State image has an invalid size or header: ${size}, ${header}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_SNAPSHOT_LOAD_PATH=${snapshot}" UKNCBTL_TEST_BOARD_OPERATIONS=1 "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL concurrent state-load smoke test failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_SNAPSHOT_LOAD_PATH=${snapshot}" UKNCBTL_TEST_SNAPSHOT_LOAD_CANCEL=1
  UKNCBTL_TEST_BOARD_OPERATIONS=1
  "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL state-load reset cancellation failed (${result}): ${error}")
endif()
file(REMOVE "${snapshot}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_SNAPSHOT_PATH=${snapshot}" "UKNCBTL_TEST_SCREENSHOT_PATH=${snapshot}"
  UKNCBTL_TEST_SCREENSHOT_CONFLICT=1 "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0 OR NOT EXISTS "${snapshot}")
  message(FATAL_ERROR "State/screenshot conflict smoke test failed (${result}): ${error}")
endif()
file(SIZE "${snapshot}" size)
file(READ "${snapshot}" header OFFSET 0 LIMIT 12 HEX)
if(NOT size EQUAL 229888 OR NOT header STREQUAL "554b4e4342544c2101000100")
  message(FATAL_ERROR "State image was modified by the conflicting screenshot request")
endif()
file(REMOVE "${snapshot}")
