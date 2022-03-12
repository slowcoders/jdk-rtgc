#ifndef SHARE_GC_RTGC_RTGCHEAP_HPP
#define SHARE_GC_RTGC_RTGCHEAP_HPP

#include "utilities/macros.hpp"
#include "memory/allStatic.hpp"

#define USE_RTGC                      true
#define USE_RTGC_COMPACT_0            false
#define USE_RTGC_COMPACT_1            true
#define USE_RTGC_TLAB_ALLOC           false
#define RTGC_TRACK_ALL_GENERATION     false

#define RTGC_OPT_CLD_SCAN             true
#define RTGC_OPT_YOUNG_ROOTS          true
#define RTGC_NO_DIRTY_CARD_MARKING    true
#define USE_RTGC_BARRIERSET_ASSEMBLER (true && USE_RTGC)
#define USE_RTGC_BARRIERSET_C1        (true && USE_RTGC)
#define USE_RTGC_BARRIERSET           (true && USE_RTGC)

#define RTGC_EXPLICT_NULL_CHCECK_ALWAYS true

#ifdef USE_RTGC
  #define RTGC_ONLY(t)                  t
#else
  #define RTGC_ONLY(t)                  
#endif

class Thread;
class oopDesc;
class OopIterateClosure;

static const int CHECK_EMPTY_TRACKBLE = 1;

class rtHeap : AllStatic {
public:
  static bool is_trackable(oopDesc* p);
  static bool is_alive(oopDesc* p);

  // for younger object collection
  static void mark_promoted_trackable(oopDesc* new_p);
  static void add_trackable_link(oopDesc* anchor, oopDesc* linked, bool is_young_root);
  static void iterate_young_roots(BoolObjectClosure* closure);

  // for full gc
  static void refresh_young_roots();
  static void adjust_tracking_pointers(oopDesc* p, bool remove_garbage);
  static void mark_pending_trackable(oopDesc* old_p, void* new_p);
  static bool flush_pending_trackables();
  static void destrory_trackable(oopDesc* p);

  // just for debugging
  static void print_heap_after_gc(bool full_gc);
  static void mark_empty_trackable(oopDesc* p);
  static HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
};



#endif // SHARE_GC_RTGC_RTGCHEAP_HPP