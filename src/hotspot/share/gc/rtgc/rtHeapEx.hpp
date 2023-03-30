#ifndef SHARE_GC_RTGC_RTREFPROCESSOR_HPP
#define SHARE_GC_RTGC_RTREFPROCESSOR_HPP

#include "rtgcHeap.hpp"

class RefProcProxyTask;

#define TRACE_UPDATE_LOG true

namespace RTGC {

class GCObject;
class ShortOOP;

#if TRACE_UPDATE_LOG
  extern int g_inverse_graph_update_cnt;
  extern int g_field_update_cnt;
#endif

class RtHashLock {
  int32_t _hash;

  static int allocateHashSlot(ShortOOP* first);
  static bool isCodeFixed(int32_t hash);
  void releaseHash();

public:
  RtHashLock();
  ~RtHashLock();

  intptr_t initHash(markWord mark);
  intptr_t hash();
  void consumeHash(intptr_t hash);
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

  static bool mark_and_clear_young_finalize_reachables(bool is_full_gc);

  static jlong _soft_ref_timestamp_clock;

  static bool g_lock_unsafe_list;
  static bool g_lock_garbage_list;
};

};
#endif // SHARE_GC_RTGC_RTREFPROCESSOR_HPP