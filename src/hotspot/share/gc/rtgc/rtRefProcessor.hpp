#ifndef SHARE_GC_RTGC_RTREFPROCESSOR_HPP
#define SHARE_GC_RTGC_RTREFPROCESSOR_HPP

#include "rtgcHeap.hpp"
#include "gc/rtgc/impl/GCObject.hpp"

namespace RTGC {

class rtHeapEx {
public:  
  static void adjust_ref_q_pointers(bool is_full_gc);

  static void initializeRefProcessor();

  static void validate_trackable_refs();

  static void invalidate_soft_weak_references(ReferenceType clear_type);

  static void update_soft_ref_master_clock();

  static void mark_ghost_anchors(GCObject* node, int depth = 0);

  static jlong _soft_ref_timestamp_clock;
};

};
#endif // SHARE_GC_RTGC_RTREFPROCESSOR_HPP