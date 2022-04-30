#ifndef SHARE_GC_RTGC_RTGCDEBUG_HPP
#define SHARE_GC_RTGC_RTGCDEBUG_HPP

#include <stdio.h>

#define RTGC_DEBUG true


namespace RTGC {
  static const int LOG_CATEGORY_BASE = 0x1000000;
  static const int LOG_FUNCTION_MASK = (LOG_CATEGORY_BASE-1);
  static const int LOG_BARRIER    = 1;
  static const int LOG_BARRIER_C1 = 2;
  static const int LOG_HEAP       = 3;
  static const int LOG_REF_LINK   = 4;
  static const int LOG_GCNODE     = 5;
  static const int LOG_SCANNER    = 6;

  inline int LOG_OPTION(int category, int function) {
    return LOG_CATEGORY_BASE * category + (1 << function);
  }

  bool logEnabled(int logOption);
  void enableLog(int category, int functions);

  const char* baseFileName(const char* filePath);
  const void* currentThreadId();

  extern volatile int* logOptions;
  extern volatile int* debugOptions;
  extern volatile void* debug_obj;
  extern volatile void* debug_obj2;
  extern bool REF_LINK_ENABLED;

  void mark_dead_space(void* obj);
  bool is_young_root(void* obj);
  void print_anchor_list(void* obj);
  bool is_debug_pointer(void* obj);
  void adjust_debug_pointer(void* old_p, void* new_p);
};


#define rtgc_log(logOption, ...) \
  if (RTGC_DEBUG && RTGC::logEnabled(logOption)) { \
    printf("%p] %s:%d ", RTGC::currentThreadId(), RTGC::baseFileName(__FILE__), __LINE__); \
    printf(__VA_ARGS__); \
  }

#define rtgc_debug_log(obj, ...) \
  rtgc_log(RTGC::is_debug_pointer((void*)obj), __VA_ARGS__)

#define rtgc_trace(opt, ...)  rtgc_log(RTGC::debugOptions[opt], __VA_ARGS__)

#endif // SHARE_GC_RTGC_RTGCDEBUG_HPP
