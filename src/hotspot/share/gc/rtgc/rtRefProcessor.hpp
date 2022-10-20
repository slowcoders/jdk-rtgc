#ifndef SHARE_GC_RTGC_RTREFPROCESSOR_HPP
#define SHARE_GC_RTGC_RTREFPROCESSOR_HPP

#include "rtgcHeap.hpp"
#include "gc/rtgc/impl/GCObject.hpp"

class RefProcProxyTask;

namespace RTGC {

class rtHeapEx {
public:  
  static void adjust_ref_q_pointers(bool is_full_gc);

  static void initializeRefProcessor();

  static void validate_trackable_refs();

  static void invalidate_soft_weak_references(ReferenceType clear_type);

  static void update_soft_ref_master_clock();

  static bool print_ghost_anchors(GCObject* node, int depth = 0);

  static void keep_alive_final_referents(RefProcProxyTask* proxy_task);

  static void break_reference_links(ReferencePolicy* policy);

  static jlong _soft_ref_timestamp_clock;
};

};
#endif // SHARE_GC_RTGC_RTREFPROCESSOR_HPP