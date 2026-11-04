// Platform detection shared by the sources.
#pragma once

#if defined(_WIN32)
#  include <winapifamily.h>
#  if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#    define PTF_WIN_DESKTOP 1
#  endif
#elif defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_OSX
#    define PTF_MACOS 1
#  else
#    define PTF_IOS 1
#  endif
#elif defined(__ANDROID__)
#  define PTF_ANDROID 1
#elif defined(__linux__) || defined(__FreeBSD__)
#  define PTF_LINUX_DESKTOP 1
#endif
