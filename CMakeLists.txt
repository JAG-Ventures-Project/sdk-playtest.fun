cmake_minimum_required(VERSION 3.16)
project(playtestfun VERSION 1.0.1 LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

option(PTF_BUILD_STATIC   "Static library for the C++ API (custom engines)" ON)
option(PTF_BUILD_SHARED   "Shared library for the C API (Unity, FFI)" ON)
option(PTF_BUILD_EXAMPLES "Build examples" ON)
option(PTF_BUILD_TESTS    "Build offline tests" ON)
option(PTF_WITH_CURL      "Use libcurl where no native transport exists" ON)

set(PTF_SOURCES
  src/client.cpp
  src/session.cpp
  src/events.cpp
  src/json.cpp
  src/util.cpp
  src/open_url.cpp
  src/transport.cpp
  src/transport_winhttp.cpp
  src/transport_curl.cpp
  src/c_api.cpp
)
if(APPLE)
  list(APPEND PTF_SOURCES src/transport_apple.mm)
endif()

find_package(Threads REQUIRED)
if(PTF_WITH_CURL AND NOT WIN32 AND NOT APPLE)
  find_package(CURL QUIET)
endif()

function(ptf_configure target)
  target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(${target} PRIVATE Threads::Threads)

  if(WIN32)
    target_link_libraries(${target} PRIVATE winhttp shell32)
  elseif(APPLE)
    target_link_libraries(${target} PRIVATE "-framework Foundation")
  elseif(CURL_FOUND)
    target_link_libraries(${target} PRIVATE CURL::libcurl)
    target_compile_definitions(${target} PRIVATE PTF_USE_CURL=1)
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

if(PTF_BUILD_STATIC)
  add_library(playtestfun STATIC ${PTF_SOURCES})
  ptf_configure(playtestfun)
  if(MSVC)
    set_target_properties(playtestfun PROPERTIES OUTPUT_NAME playtestfun_static)  # avoid import lib clash
  endif()
  add_library(playtestfun::playtestfun ALIAS playtestfun)
endif()

if(PTF_BUILD_SHARED)
  if(APPLE AND NOT IOS)
    add_library(playtestfun_shared MODULE ${PTF_SOURCES})  # .bundle for Unity macOS
    set_target_properties(playtestfun_shared PROPERTIES BUNDLE ON BUNDLE_EXTENSION bundle)
  else()
    add_library(playtestfun_shared SHARED ${PTF_SOURCES})
  endif()
  ptf_configure(playtestfun_shared)
  target_compile_definitions(playtestfun_shared PRIVATE PTF_SHARED_BUILD=1 INTERFACE PTF_SHARED=1)
  set_target_properties(playtestfun_shared PROPERTIES OUTPUT_NAME playtestfun)
endif()

if(PTF_BUILD_EXAMPLES AND PTF_BUILD_STATIC)
  add_executable(ptf_custom_engine examples/custom_engine/main.cpp)
  target_link_libraries(ptf_custom_engine PRIVATE playtestfun)
endif()

if(PTF_BUILD_TESTS AND PTF_BUILD_STATIC)
  enable_testing()
  add_executable(ptf_tests tests/tests.cpp)
  target_include_directories(ptf_tests PRIVATE src)
  target_link_libraries(ptf_tests PRIVATE playtestfun)
  add_test(NAME ptf_tests COMMAND ptf_tests)
endif()

if(NOT WIN32 AND NOT APPLE AND NOT CURL_FOUND)
  message(STATUS "playtestfun: libcurl not found, inject a ptf::Transport (Config::transport / ptf_config.http)")
endif()
