#ifndef SHARE_GC_RTGC_RTREFPROCESSOR_HPP
#define SHARE_GC_RTGC_RTREFPROCESSOR_HPP

#include "rtgcHeap.hpp"
#include "gc/rtgc/impl/GCObject.hpp"

namespace RTGC {

class rtHeapEx : rtHeap {
public:  
  static void adjust_ref_q_pointers(bool is_full_gc);

  template <ReferenceType scanType>
  static bool removeWeakAnchors(GCObject* node);

  template <ReferenceType scanType>
  static bool clear_garbage_links_and_weak_anchors(GCObject* link, GCObject* garbageAnchor);

  static void validate_trackable_refs();
};

};
#endif // SHARE_GC_RTGC_RTREFPROCESSOR_HPP