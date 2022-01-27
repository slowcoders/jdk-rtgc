#ifndef SHARE_GC_RTGC_RTGCDEBUG_HPP
#define SHARE_GC_RTGC_RTGCDEBUG_HPP

#include <string.h>

#define RTGC_DEBUG true

namespace RTGC {
  struct DebugOption {
    int logLevel;
    int traceLevel;
    int opt1;
    int opt2;
  };
  extern volatile DebugOption* debug;

  inline int logLevel() {
    return !RTGC_DEBUG ? -1 : debug->logLevel;
  }
  inline int traceLevel() {
    return !RTGC_DEBUG ? -1 : debug->traceLevel;
  }
  inline int opt1() {
    return !RTGC_DEBUG ? -1 : debug->opt1;
  }
  inline int opt2() {
    return !RTGC_DEBUG ? -1 : debug->opt2;
  }

  inline const char* baseFileName(const char* filePath) {
    const char* name = strrchr(__FILE__, '/');
    return name ? name + 1: filePath;
  }
};

#define rtgc_log(_logLevel, cond, ...) \
  if ((cond) && RTGC_DEBUG && RTGC::logLevel() >= _logLevel) { \
    printf("%s:%d ", RTGC::baseFileName(__FILE__), __LINE__); \
    printf(__VA_ARGS__); \
  }


#endif // SHARE_GC_RTGC_RTGCDEBUG_HPP
