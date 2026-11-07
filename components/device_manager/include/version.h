#pragma once
#include <stdint.h>

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 2
#define FW_VERSION_BUILD 1

#define __FW_STR_(x) #x
#define __FW_STR(x)  __FW_STR_(x)

#define FW_VERSION_STRING \
  __FW_STR(FW_VERSION_MAJOR) "." \
  __FW_STR(FW_VERSION_MINOR) "." \
  __FW_STR(FW_VERSION_PATCH) "." \
  __FW_STR(FW_VERSION_BUILD)

inline const char *firmware_version() { return FW_VERSION_STRING; }