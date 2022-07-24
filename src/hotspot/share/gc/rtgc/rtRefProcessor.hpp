#ifndef SHARE_GC_RTGC_RTREFPROCESSOR_HPP
#define SHARE_GC_RTGC_RTREFPROCESSOR_HPP

#include "rtgcHeap.hpp"

class rtHeapEx : rtHeap {
public:  
  static void clear_phantom_references(bool is_full_gc);
};

#endif // SHARE_GC_RTGC_RTREFPROCESSOR_HPP