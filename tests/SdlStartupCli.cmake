file(MAKE_DIRECTORY "${TEST_DIR}")
string(REPEAT "x" 24576 cartridge)
file(WRITE "${TEST_DIR}/cart-日本.rom" "${cartridge}")
string(ASCII 8 2 header)
string(REPEAT "x" 32766 body)
file(WRITE "${TEST_DIR}/disk-日本.img" "${header}${body}")
file(WRITE "${TEST_DIR}/floppy-日本.dsk" "${header}${body}")

execute_process(COMMAND "${APP}" --help RESULT_VARIABLE result OUTPUT_VARIABLE output)
if(NOT result EQUAL 0 OR NOT output MATCHES "--boot")
  message(FATAL_ERROR "Help failed: ${result}: ${output}")
endif()
execute_process(COMMAND "${APP}" --boot 9 RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 2 OR NOT error MATCHES "--boot")
  message(FATAL_ERROR "Invalid boot was not rejected: ${result}: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --smoke-test --hard-disk 1 "${TEST_DIR}/disk-日本.img"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 1)
  message(FATAL_ERROR "Hard disk without cartridge was not rejected: ${result}: ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${APP}" --smoke-test --no-sound --boot 1 --floppy 3 "${TEST_DIR}/floppy-日本.dsk"
  --cartridge 1 "${TEST_DIR}/cart-日本.rom" --hard-disk 1 "${TEST_DIR}/disk-日本.img"
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Unicode media startup failed: ${result}: ${error}")
endif()
file(REMOVE "${TEST_DIR}/cart-日本.rom" "${TEST_DIR}/disk-日本.img" "${TEST_DIR}/floppy-日本.dsk")
