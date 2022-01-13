#ifndef __RTGC_HPP__
#define __RTGC_HPP__

#include "oops/accessDecorators.hpp"
#include "oops/compressedOops.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/atomic.hpp"
#include <string.h>

#define RTGC_DEBUG true

namespace RTGC {
  extern volatile int ENABLE_LOG;
  extern volatile int ENABLE_TRACE;

  bool isPublished(oopDesc* obj);

  bool lock_heap(oopDesc* obj);

  void unlock_heap(bool locked);

  void add_referrer(oopDesc* obj, oopDesc* referrer);

  void remove_referrer(oopDesc* obj, oopDesc* referrer);

  inline const char* baseFileName(const char* filePath) {
    const char* name = strrchr(__FILE__, '/');
    return name ? name + 1: filePath;
  }
};

#define rtgc_log(cond, ...) \
  if ((cond) && RTGC_DEBUG && RTGC::ENABLE_LOG) { \
    printf("%s:%d ", RTGC::baseFileName(__FILE__), __LINE__); \
    printf(__VA_ARGS__); \
  }



#endif // __RTGC_HPP__
