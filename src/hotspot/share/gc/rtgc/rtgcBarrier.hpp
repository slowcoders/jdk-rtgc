
#ifndef SHARE_GC_RTGC_RTGCBARRIER_HPP
#define SHARE_GC_RTGC_RTGCBARRIER_HPP

#include "memory/allocation.hpp"
#include "oops/oop.hpp"

class RtgcBarrier : public AllStatic {
public:
  static oop  oop_cmpxchg(oop base, volatile narrowOop* p, oop comapre_value, oop new_value);
  static oop  oop_cmpxchg(oop base, volatile oop* p, oop comapre_value, oop new_value);

  static oop  oop_cmpxchg_in_root(volatile narrowOop* p, oop comapre_value, oop new_value);
  static oop  oop_cmpxchg_in_root(volatile oop* p, oop comapre_value, oop new_value);

  static oop  oop_xchg(oop base, volatile narrowOop* p, oop new_value);
  static oop  oop_xchg(oop base, volatile oop* p, oop new_value);

  static oop  oop_xchg_in_root(volatile narrowOop* p, oop new_value);
  static oop  oop_xchg_in_root(volatile oop* p, oop new_value);

  static void clone_barrier_post(arrayOop array);

  static void oop_arraycopy_nocheck(arrayOop dst_array, oop* dst_p, oop* src_p, size_t length);
  static void oop_arraycopy_nocheck(arrayOop dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length);
  static void oop_arraycopy_nocheck(arrayOop dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length);

  static bool oop_arraycopy_checkcast(arrayOop dst_array, oop* dst_p, oop* src_p, size_t length);
  static bool oop_arraycopy_checkcast(arrayOop dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length);
  static bool oop_arraycopy_checkcast(arrayOop dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length);

};

#endif // SHARE_GC_RTGC_RTGCBARRIER_HPP
