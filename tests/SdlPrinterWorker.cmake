file(MAKE_DIRECTORY "${TEST_DIR}")
set(path "${TEST_DIR}/printer-日本.log")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_PRINTER_PATH=${path}" "${APP}" --smoke-test
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0" OR NOT EXISTS "${path}")
  message(FATAL_ERROR "SDL printer output failed (${result}): ${error}")
endif()
file(READ "${path}" output HEX)
if(NOT output STREQUAL "42")
  message(FATAL_ERROR "Unexpected printer output: ${output}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_PRINTER_PATH=${path}" UKNCBTL_TEST_BOARD_OPERATIONS=1 "${APP}" --smoke-test
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "SDL event pumping during printer start, pause and stop failed (${result}): ${error}")
endif()
file(READ "${path}" output HEX)
if(NOT output STREQUAL "42")
  message(FATAL_ERROR "Unexpected printer output after pause and stop: ${output}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_PRINTER_PATH=${path}" UKNCBTL_TEST_PRINTER_FAILURE_EXIT=1 "${APP}" --smoke-test
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result STREQUAL "0" OR NOT error MATCHES "Could not finish the printer output file")
  message(FATAL_ERROR "SDL did not report the worker failure on exit (${result}): ${error}")
endif()
file(REMOVE "${path}")
