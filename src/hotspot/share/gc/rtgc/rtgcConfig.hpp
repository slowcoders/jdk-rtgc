#ifndef SHARE_GC_RTGC_RTGCCONFIG_HPP
#define SHARE_GC_RTGC_RTGCCONFIG_HPP

#define USE_RTGC                      true

#define USE_RTGC_BARRIERSET_ASSEMBLER (true && USE_RTGC)
#define USE_RTGC_BARRIERSET_C1        (true && USE_RTGC)
#define USE_RTGC_BARRIERSET           (true && USE_RTGC)

#define RTGC_EXPLICT_NULL_CHCECK_ALWAYS true

class Thread;

namespace RTGC {
  HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
};

#endif // SHARE_GC_RTGC_