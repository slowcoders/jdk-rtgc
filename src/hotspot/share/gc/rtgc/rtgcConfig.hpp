#ifndef SHARE_GC_RTGC_RTGCCONFIG_HPP
#define SHARE_GC_RTGC_RTGCCONFIG_HPP

#define USE_RTGC                      true
#define USE_RTGC_COMPACT_0            false
#define USE_RTGC_TLAB_ALLOC           false
#define RTGC_NO_TRACE_YOUNGER_GENERATION  true

#define USE_RTGC_BARRIERSET_ASSEMBLER (true && USE_RTGC)
#define USE_RTGC_BARRIERSET_C1        (true && USE_RTGC)
#define USE_RTGC_BARRIERSET           (true && USE_RTGC)

#define RTGC_EXPLICT_NULL_CHCECK_ALWAYS true

class Thread;
class oopDesc;

namespace RTGC {
  HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
  void adjust_pointers(oopDesc* obj, void* newOop);
  void register_old_object(oopDesc* obj, void* newOop);
};

#endif // SHARE_GC_RTGC_