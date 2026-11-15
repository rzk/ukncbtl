include(FetchContent)

if(UKNCBTL_ENABLE_SDL3)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  # SDL 3.4 includes PNG encoding, avoiding an additional image library.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68)
  FetchContent_MakeAvailable(SDL3)
endif()

if(UKNCBTL_ENABLE_IMGUI)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab) # v1.92.9b
  FetchContent_MakeAvailable(imgui)
endif()

# Pin release commits so repeated builds use the same sources. FetchContent's
# FETCHCONTENT_SOURCE_DIR_<NAME> overrides also support offline checkouts.
if(BUILD_TESTING)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723) # v1.17.0
  FetchContent_MakeAvailable(googletest)
  # Newer Clang versions diagnose a conversion in the pinned GoogleTest
  # release. Keep the dependency's warnings from becoming build errors.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(gtest PRIVATE -Wno-error=character-conversion)
  endif()
endif()

if(UKNCBTL_BUILD_BENCHMARKS)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG 31c66f47f60d06790a4885a261d682ba339bd100) # v1.9.4
  FetchContent_MakeAvailable(googlebenchmark)
endif()

if(UKNCBTL_ENABLE_TRACY)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  set(TRACY_STATIC ON CACHE BOOL "" FORCE)
  set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
  FetchContent_Declare(tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG c556831ddc6fe26d2fce01c14c97205a9dad46d5) # v0.12.2
  FetchContent_MakeAvailable(tracy)
endif()
