# Copies the core into the Unreal plugin (UBT only builds files inside the module).
# Usage: cmake -P tools/sync_unreal.cmake
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(DEST "${ROOT}/engines/unreal/PlaytestFun/Source/PlaytestFun/Private/Core")

file(REMOVE_RECURSE "${DEST}")
file(COPY "${ROOT}/include" DESTINATION "${DEST}")
file(COPY "${ROOT}/src" DESTINATION "${DEST}")
message(STATUS "playtestfun core synced to ${DEST}")
