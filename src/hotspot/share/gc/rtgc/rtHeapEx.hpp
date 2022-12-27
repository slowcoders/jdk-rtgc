#ifndef SHARE_GC_RTGC_RTREFPROCESSOR_HPP
#define SHARE_GC_RTGC_RTREFPROCESSOR_HPP

#include "rtgcHeap.hpp"

class RefProcProxyTask;

extern bool rtHeapEx__useModifyFlag;

namespace RTGC {

class GCObject;

class RtHashLock {
  int32_t _hash;

  void releaseHash();

public:
  RtHashLock() { clearHash(); }
  ~RtHashLock();

  static bool isLocked(intptr_t hash);
  intptr_t makeHash(intptr_t hash);
  void clearHash() { _hash = 0; }
};

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

  static void mark_immortal_heap_objects();

  static void check_immortal_heap_objects();

  static jlong _soft_ref_timestamp_clock;

  static bool g_lock_unsafe_list;
  static bool g_lock_garbage_list;
};

};
#endif // SHARE_GC_RTGC_RTREFPROCESSOR_HPP