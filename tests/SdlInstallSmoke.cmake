if(NOT DEFINED BUILD_DIR OR NOT DEFINED TEST_DIR OR NOT DEFINED EXE_NAME)
  message(FATAL_ERROR "BUILD_DIR, TEST_DIR and EXE_NAME are required")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(install_dir "${TEST_DIR}/${run_id}")
set(install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${install_dir}")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
  list(APPEND install_command --config "${CONFIG}")
endif()
execute_process(COMMAND ${install_command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Installation failed: ${output}\n${error}")
endif()

foreach(file IN ITEMS "bin/${EXE_NAME}" licenses/UKNCBTL-LICENSE licenses/SDL3-LICENSE
    licenses/IMGUI-LICENSE licenses/NOTICE-DroidSans licenses/LICENSE-Apache-2.0 licenses/NOTICE-RenderVfw)
  if(NOT EXISTS "${install_dir}/${file}")
    message(FATAL_ERROR "Installed file is missing: ${file}")
  endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  "${install_dir}/bin/${EXE_NAME}" --smoke-test
  WORKING_DIRECTORY "${install_dir}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Installed frontend smoke test failed (${result}): ${output}\n${error}")
endif()
file(REMOVE_RECURSE "${install_dir}")
