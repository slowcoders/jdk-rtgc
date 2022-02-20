#ifndef SHARE_GC_RTGC_RTGCHEAP_HPP
#define SHARE_GC_RTGC_RTGCHEAP_HPP

#include "utilities/macros.hpp"

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

namespace RTGC {
  void mark_pending_trackable(oopDesc* old_p, void* new_p);
  void mark_promoted_trackable(oopDesc* old_p, oopDesc* new_p);
  void unmark_trackable(oopDesc* obj);

  // should be called before pointer adjusting
  void refresh_young_roots();

  // should be called for each marked object
  void adjust_pointers(oopDesc* obj);

  // should be called after heap compaction finished
  void flush_trackables();

  HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
  void iterate_young_roots(DefNewYoungerGenClosure* closer);
};

#endif // SHARE_GC_RTGC_RTGCHEAP_HPP