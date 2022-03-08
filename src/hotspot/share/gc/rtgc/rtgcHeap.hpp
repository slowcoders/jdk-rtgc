#ifndef SHARE_GC_RTGC_RTGCHEAP_HPP
#define SHARE_GC_RTGC_RTGCHEAP_HPP

#include "utilities/macros.hpp"
#include "memory/allStatic.hpp"

#define USE_RTGC                      true
#define USE_RTGC_COMPACT_0            false
#define USE_RTGC_COMPACT_1            true
#define USE_RTGC_TLAB_ALLOC           false
#define RTGC_TRACK_ALL_GENERATION     false

#define RTGC_OPT_YOUNG_ROOTS          false
#define RTGC_OPT_CLD_SCAN             false
#define RTGC_NO_DIRTY_CARD_MARKING    false
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

class rtHeap : AllStatic {
public:
  static void mark_active_trackable(oopDesc* p);
  static void mark_empty_trackable(oopDesc* p);
  static void mark_pending_trackable(oopDesc* old_p, void* new_p);
  static void mark_promoted_trackable(oopDesc* new_p);

  static bool is_trackable(oopDesc* p);
  static bool is_alive(oopDesc* p);
  static void destrory_trackable(oopDesc* p);

  // should be called before pointer adjusting
  static void refresh_young_roots(bool is_object_moved);

  // should be called for each marked object
  static void adjust_tracking_pointers(oopDesc* p, bool remove_garbage);

  // should be called after heap compaction finished
  static void flush_trackables();

  static void print_heap_after_gc(bool full_gc);

  static HeapWord* allocate_tlab(Thread* thread, const size_t word_size);

  static void add_trackable_link(oopDesc* anchor, oopDesc* linked);

  static void iterate_young_roots(BoolObjectClosure* closure);
};



#endif // SHARE_GC_RTGC_RTGCHEAP_HPP