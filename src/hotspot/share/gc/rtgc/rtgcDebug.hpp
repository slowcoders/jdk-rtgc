#ifndef SHARE_GC_RTGC_RTGCDEBUG_HPP
#define SHARE_GC_RTGC_RTGCDEBUG_HPP

#include <stdio.h>

#define RTGC_DEBUG true


namespace RTGC {
  static const int LOG_CATEGORY_BASE = 0x1000000;
  static const int LOG_FUNCTION_MASK = (LOG_CATEGORY_BASE-1);
  inline int LOG_OPTION(int category, int function) {
    return LOG_CATEGORY_BASE * category + (1 << function);
  }
  inline int LOG_CATEGORY(int option) {
    return option / LOG_CATEGORY_BASE;
  }
  inline int LOG_FUNCTION(int option) {
    return option & (LOG_CATEGORY_BASE - 1);
  }

  bool logEnabled(int logOption);
  void enableLog(int category, int functions);

  const char* baseFileName(const char* filePath);

  struct DebugOptions {
    int opt1;
    int opt2;
    int opt3;
  };

  extern volatile DebugOptions* debugOptions;
};

#define rtgc_log(logOption, ...) \
  if (RTGC_DEBUG && RTGC::logEnabled(logOption)) { \
    printf("%s:%d ", RTGC::baseFileName(__FILE__), __LINE__); \
    printf(__VA_ARGS__); \
  }


#endif // SHARE_GC_RTGC_RTGCDEBUG_HPP
