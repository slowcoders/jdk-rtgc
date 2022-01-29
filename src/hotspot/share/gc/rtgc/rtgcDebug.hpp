#ifndef SHARE_GC_RTGC_RTGCDEBUG_HPP
#define SHARE_GC_RTGC_RTGCDEBUG_HPP

#include <stdio.h>

#define RTGC_DEBUG true


namespace RTGC {
  static const int LOG_CATEGORY_BASE = 0x1000000;
  static const int LOG_FUNCTION_MASK = (LOG_CATEGORY_BASE-1);
  static const int LOG_OPTION(int category, int function) {
    return LOG_CATEGORY_BASE * category + (1 << function);
  }
  static const int LOG_CATEGORY(int option) {
    return option / LOG_CATEGORY_BASE;
  }
  static const int LOG_FUNCTION(int option) {
    return option & (LOG_CATEGORY_BASE - 1);
  }

  bool logEnabled(int logOption);
  void enableLog(int category, int functions);

  const char* baseFileName(const char* filePath);
};

#define rtgc_log(logOption, ...) \
  if (RTGC_DEBUG && RTGC::logEnabled(logOption)) { \
    printf("%s:%d ", RTGC::baseFileName(__FILE__), __LINE__); \
    printf(__VA_ARGS__); \
  }


#endif // SHARE_GC_RTGC_RTGCDEBUG_HPP
