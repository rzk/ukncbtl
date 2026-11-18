file(MAKE_DIRECTORY "${TEST_DIR}")
foreach(kind IN ITEMS memory display console)
  set(path "${TEST_DIR}/${kind}-日本.dat")
  file(REMOVE "${path}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
    "UKNCBTL_TEST_EXPORT_KIND=${kind}" "UKNCBTL_TEST_EXPORT_PATH=${path}"
    "UKNCBTL_TEST_SCREENSHOT_PATH=${path}" UKNCBTL_TEST_SCREENSHOT_CONFLICT=1
    "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
  if(NOT result EQUAL 0 OR NOT EXISTS "${path}")
    message(FATAL_ERROR "SDL ${kind} export failed (${result}): ${error}")
  endif()
  file(SIZE "${path}" size)
  if(size EQUAL 0 OR (kind STREQUAL "memory" AND NOT size EQUAL 64))
    message(FATAL_ERROR "Unexpected ${kind} export size: ${size}")
  endif()
  if(kind STREQUAL "display")
    file(READ "${path}" prefix LIMIT 12 HEX)
    if(NOT prefix STREQUAL "6c696e652061646472657373")
      message(FATAL_ERROR "Unexpected display-list header: ${prefix}")
    endif()
  endif()
  file(REMOVE "${path}")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_EXPORT_KIND=console" "UKNCBTL_TEST_EXPORT_PATH=${TEST_DIR}/missing/log.txt"
  UKNCBTL_TEST_EXPORT_FAILURE_EXIT=1 "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0 OR NOT error MATCHES "Could not save the debugger export")
  message(FATAL_ERROR "SDL export failure was not reported on exit (${result}): ${error}")
endif()
