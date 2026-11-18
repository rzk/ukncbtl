file(MAKE_DIRECTORY "${TEST_DIR}")
set(disk "${TEST_DIR}/worker-日本.dsk")
file(REMOVE "${disk}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_DISK_PATH=${disk}" "UKNCBTL_TEST_SCREENSHOT_PATH=${disk}"
  UKNCBTL_TEST_SCREENSHOT_CONFLICT=1 "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0 OR NOT EXISTS "${disk}")
  message(FATAL_ERROR "SDL disk creation failed (${result}): ${error}")
endif()
file(SIZE "${disk}" size)
if(NOT size EQUAL 819200)
  message(FATAL_ERROR "Incorrect disk image size: ${size}")
endif()
file(REMOVE "${disk}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_DISK_PATH=${TEST_DIR}/missing/worker.dsk" UKNCBTL_TEST_DISK_FAILURE_EXIT=1
  "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0 OR NOT error MATCHES "Could not create the disk image")
  message(FATAL_ERROR "SDL disk failure was not reported on exit (${result}): ${error}")
endif()
