# Builds the C API library for the host platform and copies it into the Unity package.
# Usage: cmake -P tools/build_unity.cmake
# Windows uses the static CRT, so players need no VC++ redistributable.
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(BUILD "${ROOT}/build/unity")
set(PLUGINS "${ROOT}/engines/unity/Runtime/Plugins")

set(ARGS -DPTF_BUILD_STATIC=OFF -DPTF_BUILD_EXAMPLES=OFF -DPTF_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release)
if(CMAKE_HOST_WIN32)
  list(APPEND ARGS -A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded)
elseif(CMAKE_HOST_APPLE)
  list(APPEND ARGS "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${ROOT}" -B "${BUILD}" ${ARGS} RESULT_VARIABLE rc)
if(rc)
  message(FATAL_ERROR "configure failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD}" --config Release RESULT_VARIABLE rc)
if(rc)
  message(FATAL_ERROR "build failed")
endif()

if(CMAKE_HOST_WIN32)
  set(SRC "${BUILD}/Release/playtestfun.dll")
  set(DEST "${PLUGINS}/x86_64")
elseif(CMAKE_HOST_APPLE)
  set(SRC "${BUILD}/playtestfun.bundle")
  set(DEST "${PLUGINS}/macOS")
else()
  set(SRC "${BUILD}/libplaytestfun.so")
  set(DEST "${PLUGINS}/Linux/x86_64")
endif()

file(MAKE_DIRECTORY "${DEST}")
file(COPY "${SRC}" DESTINATION "${DEST}")
message(STATUS "playtestfun Unity plugin copied to ${DEST}")
