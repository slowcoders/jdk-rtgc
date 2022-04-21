#ifndef SHARE_GC_RTGC_RTGCHEAP_HPP
#define SHARE_GC_RTGC_RTGCHEAP_HPP

#include "utilities/macros.hpp"
#include "memory/allStatic.hpp"

#define USE_RTGC                      true
#define USE_RTGC_COMPACT_0            false
#define USE_RTGC_COMPACT_1            true
#define USE_RTGC_TLAB_ALLOC           false
#define RTGC_TRACK_ALL_GENERATION     false

#define RTGC_REMOVE_GARBAGE_REFERRER_ON_ADJUST_POINTER true

#define RTGC_PARALLEL		              false

#define RTGC_OPT_PHANTOM_REF          true
#define RTGC_IGNORE_JREF              false
#define RTGC_OPT_CLD_SCAN             true
#define RTGC_OPT_YOUNG_ROOTS          true
#define RTGC_NO_DIRTY_CARD_MARKING    true
#define RTGC_OPT_YG_SCAN              true
#define USE_RTGC_BARRIERSET_ASSEMBLER (true && USE_RTGC)
#define USE_RTGC_BARRIERSET_C1        (true && USE_RTGC)
#define USE_RTGC_BARRIERSET           (true && USE_RTGC)

#define RTGC_EXPLICT_NULL_CHCECK_ALWAYS true
#define RTGC_CHECK_EMPTY_TRACKBLE      true

class Thread;
class oopDesc;
class OopIterateClosure;
class BoolObjectClosure;
class OopClosure;
class ReferenceDiscoverer;

class rtHeap : AllStatic {
public:
  static bool is_trackable(oopDesc* p);
  static bool is_alive(oopDesc* p, bool assert_alive = false);

  // for younger object collection
  static void mark_promoted_trackable(oopDesc* new_p);
  static void add_promoted_link(oopDesc* promoted_anchor, oopDesc* linked, bool link_is_tenured);
  static void iterate_young_roots(BoolObjectClosure* young_root_closure, OopClosure* survivor_closure);
  static void mark_survivor_reachable(oopDesc* tenured_p, bool as_java_referent = false);
  static void mark_keep_alive(oopDesc* referent);
  static void add_young_root(oopDesc* old_p, oopDesc* new_p);

  // for full gc
  static void prepare_full_gc();
  static void prepare_point_adjustment(void* old_gen_heap_start);
  static size_t adjust_pointers(oopDesc* old_p);
  static void mark_pending_trackable(oopDesc* old_p, void* new_p);
  static void mark_forwarded(oopDesc* p);
  static void destroy_trackable(oopDesc* p);

  // for jni
  static void release_jni_handle(oopDesc* p);

  // for reference management
  static void init_java_reference(oopDesc* ref_oop, oopDesc* referent);
  static void link_discovered_pending_reference(oopDesc* ref_oop, oopDesc* discovered);
  static void discover_java_references(ReferenceDiscoverer* rp, bool is_tenure_gc);

  // just for debugging
  static void print_heap_after_gc(bool full_gc);
  static void mark_empty_trackable(oopDesc* p);
  static HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
};



#endif // SHARE_GC_RTGC_RTGCHEAP_HPP