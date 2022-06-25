#ifndef SHARE_GC_RTGC_SPACE_HPP
#define SHARE_GC_RTGC_SPACE_HPP

#include "gc/shared/space.hpp"

class RtSpace: public TenuredSpace {
 public:
  RtSpace(BlockOffsetSharedArray* sharedOffsetArray,
               MemRegion mr) :
    TenuredSpace(sharedOffsetArray, mr) {}
};


#endif