file(MAKE_DIRECTORY "${TEST_DIR}")
set(system "${TEST_DIR}/system-日本.bin")
set(cartridge "${TEST_DIR}/cartridge-日本.bin")
string(REPEAT "B" 24576 cartridge_data)
configure_file("${ROM_FILE}" "${system}" COPYONLY)
file(SHA256 "${system}" original_hash)
file(WRITE "${cartridge}" "${cartridge_data}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_ROM_PATH=${system}" "UKNCBTL_TEST_SCREENSHOT_PATH=${system}"
  UKNCBTL_TEST_SCREENSHOT_CONFLICT=1 "${APP}" --smoke-test --sound
  RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL system ROM load failed (${result}): ${error}")
endif()
file(SHA256 "${system}" current_hash)
if(NOT current_hash STREQUAL original_hash)
  message(FATAL_ERROR "Conflicting output changed the system ROM")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_ROM_PATH=${cartridge}" UKNCBTL_TEST_ROM_CARTRIDGE=1
  "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL cartridge load failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_ROM_PATH=${system}" UKNCBTL_TEST_ROM_CANCEL=1
  "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL ROM reset cancellation failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "UKNCBTL_TEST_ROM_PATH=${TEST_DIR}/missing.bin" UKNCBTL_TEST_ROM_FAILURE=1
  "${APP}" --smoke-test --sound RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 25)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SDL ROM failure handling failed (${result}): ${error}")
endif()
file(REMOVE "${system}" "${cartridge}")
