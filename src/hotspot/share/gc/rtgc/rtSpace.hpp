#ifndef SHARE_GC_RTGC_SPACE_HPP
#define SHARE_GC_RTGC_SPACE_HPP

#include "gc/shared/space.inline.hpp"

namespace rtHeapUtil {

  bool is_dead_space(oopDesc* obj);

  void ensure_alive_or_deadsapce(oopDesc* old_p);
}

class RtSpace: public TenuredSpace {
public:
  typedef TenuredSpace _SUPER;
  RtSpace(BlockOffsetSharedArray* sharedOffsetArray,
               MemRegion mr) :
    TenuredSpace(sharedOffsetArray, mr) {}

  virtual HeapWord* allocate(size_t word_size);
  virtual HeapWord* par_allocate(size_t word_size);

};


#endif