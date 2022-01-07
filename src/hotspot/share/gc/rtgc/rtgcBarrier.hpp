
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

  static void oop_arraycopy_nocheck(arrayOopDesc*  dst_array, oop* dst_p, oop* src_p, size_t length);
  static void oop_arraycopy_nocheck(arrayOopDesc*  dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length);
  static void oop_arraycopy_nocheck(arrayOopDesc*  dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length);

  static bool oop_arraycopy_checkcast(arrayOopDesc*  dst_array, oop* dst_p, oop* src_p, size_t length);
  static bool oop_arraycopy_checkcast(arrayOopDesc*  dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length);
  static bool oop_arraycopy_checkcast(arrayOopDesc*  dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length);

};

#endif // SHARE_GC_RTGC_RTGCBARRIER_HPP
