
#ifndef SHARE_GC_RTGC_RTGCBARRIER_HPP
#define SHARE_GC_RTGC_RTGCBARRIER_HPP

#include "memory/allocation.hpp"
#include "oops/oop.hpp"

class RtgcBarrier : public AllStatic {
public:
  static oopDesc*  oop_cmpxchg(oopDesc* base, volatile narrowOop* p, oopDesc* comapre_value, oopDesc* new_value);
  static oopDesc*  oop_cmpxchg(oopDesc* base, volatile oop* p, oopDesc* comapre_value, oopDesc* new_value);

  static oopDesc*  oop_cmpxchg_in_root(volatile narrowOop* p, oopDesc* comapre_value, oopDesc* new_value);
  static oopDesc*  oop_cmpxchg_in_root(volatile oop* p, oopDesc* comapre_value, oopDesc* new_value);

  static oopDesc*  oop_xchg(oopDesc* base, volatile narrowOop* p, oopDesc* new_value);
  static oopDesc*  oop_xchg(oopDesc* base, volatile oop* p, oopDesc* new_value);

  static oopDesc*  oop_xchg_in_root(volatile narrowOop* p, oopDesc* new_value);
  static oopDesc*  oop_xchg_in_root(volatile oop* p, oopDesc* new_value);

  static void clone_post_barrier(oopDesc* array);

  static void oop_arraycopy_nocheck(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_nocheck(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_nocheck(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array);

  /**
   * returns remaining count: 0 for success
   */
  static int oop_arraycopy_checkcast(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array);
  static int oop_arraycopy_checkcast(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
  static int oop_arraycopy_checkcast(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array);
};

#endif // SHARE_GC_RTGC_RTGCBARRIER_HPP
