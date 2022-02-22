#ifndef SHARE_GC_RTGC_RTGCHEAP_HPP
#define SHARE_GC_RTGC_RTGCHEAP_HPP

#include "utilities/macros.hpp"
#include "memory/allStatic.hpp"

#define USE_RTGC                      true
#define USE_RTGC_COMPACT_0            false
#define USE_RTGC_COMPACT_1            true
#define USE_RTGC_TLAB_ALLOC           false
#define RTGC_TRACK_ALL_GENERATION     false

#define RTGC_OPTIMIZED_YOUNGER_GENERATION_GC  false
#define USE_RTGC_BARRIERSET_ASSEMBLER (true && USE_RTGC)
#define USE_RTGC_BARRIERSET_C1        (true && USE_RTGC)
#define USE_RTGC_BARRIERSET           (true && USE_RTGC)

#define RTGC_EXPLICT_NULL_CHCECK_ALWAYS true

class Thread;
class oopDesc;
class DefNewYoungerGenClosure;

class rtHeap : AllStatic {
public:
  static void mark_active_trackable(oopDesc* p);
  static void mark_empty_trackable(oopDesc* p);
  static void mark_pending_trackable(oopDesc* old_p, void* new_p);
  static void mark_promoted_trackable(oopDesc* old_p, oopDesc* new_p);

  static bool is_trackable(oopDesc* p);
  static void destrory_trackable(oopDesc* p);

  // should be called before pointer adjusting
  static void refresh_young_roots();

  // should be called for each marked object
  static void adjust_pointers(oopDesc* p);

  // should be called after heap compaction finished
  static void flush_trackables();

  static void print_heap_after_gc();

  static HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
  static void iterate_young_roots(DefNewYoungerGenClosure* closer);
};


#endif // SHARE_GC_RTGC_RTGCHEAP_HPP