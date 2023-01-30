#ifndef SHARE_GC_RTGC_RTGC_LOG_HPP
#define SHARE_GC_RTGC_RTGC_LOG_HPP

#include <stdio.h>
#include "utilities/debug.hpp"

#ifdef ASSERT
  #define ENABLE_RTGC_ASSERT true
#else 
  #define ENABLE_RTGC_ASSERT true
#endif


#if ENABLE_RTGC_ASSERT
  extern bool RTGC_DEBUG;
#else 
  #define RTGC_DEBUG false
#endif

#if defined(ASSERT) || !ENABLE_RTGC_ASSERT
#define rt_assert_f  assert
#define rt_assert    precond
#else
#define rt_assert_f(p, ...)                                                    \
do {                                                                           \
  if (!(p)) {                                                                  \
    TOUCH_ASSERT_POISON;                                                       \
    report_vm_error(__FILE__, __LINE__, "assert(" #p ") failed", __VA_ARGS__); \
    BREAKPOINT;                                                                \
  }                                                                            \
} while (0)
#define rt_assert(p)  rt_assert_f(p, "")
#endif


namespace RTGC {
  static const int LOG_CATEGORY_BASE = 0x1000000;
  static const int LOG_FUNCTION_MASK = (LOG_CATEGORY_BASE-1);

  inline int LOG_OPTION(int category, int function) {
    return LOG_CATEGORY_BASE * category + (1 << function);
  }

  bool logEnabled(int logOption);
  void enableLog(int category, int functions);

  const char* baseFileName(const char* filePath);
  const void* currentThreadId();

  extern int*  logOptions;
  extern int*  debugOptions;
  extern void* debug_obj;
  extern void* debug_obj2;
  extern bool  REF_LINK_ENABLED;

  int  is_debug_pointer(void* obj);
  void adjust_debug_pointer(void* old_p, void* new_p, bool destroy_old_node);
};

#define PTR_DBG_SIG "%p(%s) tr=%d rc=%d ac=%d g=%d sc=[%d] cls=%d u=%d m=%d\n"
#define PTR_DBG_INFO(obj) (void*)obj, RTGC::getClassName(obj), \
  RTGC::to_obj(obj)->isTrackable(), RTGC::to_obj(obj)->getRootRefCount(), RTGC::to_obj(obj)->getReferrerCount(), \
  RTGC::to_obj(obj)->isGarbageMarked(), RTGC::to_obj(obj)->node_()->getShortcutId(), \
   cast_to_oop(obj)->klass() == vmClasses::Class_klass(), \
  RTGC::to_obj(obj)->isUnstableMarked(), cast_to_oop(obj)->is_gc_marked()

#define rtgc_log(logOption, ...) \
  if (RTGC_DEBUG && RTGC::logEnabled(logOption)) { \
    printf("%p] %s:%d ", RTGC::currentThreadId(), RTGC::baseFileName(__FILE__), __LINE__); \
    printf(__VA_ARGS__); \
  }

#define rtgc_debug_log(obj, ...) \
  rtgc_log(RTGC::is_debug_pointer((void*)obj), __VA_ARGS__)

#define rtgc_trace(opt, ...)  rtgc_log(RTGC::debugOptions[opt], __VA_ARGS__)

#endif // SHARE_GC_RTGC_RTGC_LOG_HPP
