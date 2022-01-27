#ifndef SHARE_GC_RTGC_RTGC_HPP
#define SHARE_GC_RTGC_RTGC_HPP

#include "oops/accessDecorators.hpp"
#include "oops/compressedOops.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/atomic.hpp"
#include <string.h>


namespace RTGC {

  bool isPublished(oopDesc* obj);

  bool lock_heap(oopDesc* obj);

  void unlock_heap(bool locked);

  void add_referrer(oopDesc* obj, oopDesc* referrer);

  void remove_referrer(oopDesc* obj, oopDesc* referrer);

};


#endif // SHARE_GC_RTGC_RTGC_HPP
