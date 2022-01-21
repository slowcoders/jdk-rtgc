
#ifndef SHARE_GC_RTGC_RTGCBARRIER_HPP
#define SHARE_GC_RTGC_RTGCBARRIER_HPP

#include "memory/allocation.hpp"
#include "oops/oop.hpp"

class RtgcBarrier : public AllStatic {
  static inline bool needBarrier(DecoratorSet decorators) {
    return ((AS_RAW | AS_NO_KEEPALIVE) & decorators) == 0
  }

  static inline bool needBarrier(LIRAccess& access) {
    return access.is_oop() && needBarrier(access.decorators());
  }

  static void (*rt_store)(oopDesc* base, narrowOop* p, oopDesc* new_value);
  static void (*rt_store_not_in_heap)(narrowOop* p, oopDesc* new_value);

  static oopDesc* (*rt_xchg)(oopDesc* base, volatile narrowOop* p, oopDesc* new_value);
  static oopDesc* (*rt_xchg_not_in_heap)(volatile narrowOop* p, oopDesc* new_value);

  static oopDesc* (*rt_cmpxchg)(oopDesc* base, volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value);
  static oopDesc* (*rt_cmpxchg_not_in_heap)(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value);

  static oopDesc* (*rt_load)(oopDesc* base, volatile narrowOop* p);
  static oopDesc* (*rt_load_not_in_heap)(volatile narrowOop* p);

  static int  (*rt_arraycopy_checkcast)(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_disjoint )(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_conjoint )(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);

public:
  static void init_barrier_runtime();

  static address getStoreFunction(bool in_heap);
  static address getXchgFunction(bool in_heap);
  static address getCmpXchgFunction(bool in_heap);
  static address getLoadFunction(bool in_heap);
  static address getArrayCopyFunction(DecoratorSet decorators);

  static void oop_store(oopDesc* base, oop* p, oopDesc* new_value);
  static void oop_store(oopDesc* base, narrowOop* p, oopDesc* new_value) {
    rt_store(base, p, new_value);
  }

  static void oop_store_not_in_heap(oop* p, oopDesc* new_value);
  static void oop_store_not_in_heap(narrowOop* p, oopDesc* new_value) {
    rt_store_not_in_heap(p, new_value);
  }

  static oopDesc* oop_xchg(oopDesc* base, volatile oop* p, oopDesc* new_value);
  static oopDesc* oop_xchg(oopDesc* base, volatile narrowOop* p, oopDesc* new_value) {
    return rt_xchg(base, p, new_value);
  }

  static oopDesc* oop_xchg_not_in_heap(volatile oop* p, oopDesc* new_value);
  static oopDesc* oop_xchg_not_in_heap(volatile narrowOop* p, oopDesc* new_value) {
    return rt_xchg_not_in_heap(p, new_value);
  }

  static oopDesc* oop_cmpxchg(oopDesc* base, volatile oop* p, oopDesc* cmp_value, oopDesc* new_value);
  static oopDesc* oop_cmpxchg(oopDesc* base, volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value) {
    return rt_cmpxchg(base, p, cmp_value, new_value);
  }

  static oopDesc* oop_cmpxchg_not_in_heap(volatile oop* p, oopDesc* cmp_value, oopDesc* new_value);
  static oopDesc* oop_cmpxchg_not_in_heap(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value) {
    return rt_cmpxchg_not_in_heap(p, cmp_value, new_value);
  }

  static oopDesc* oop_load(oopDesc* base, volatile oop* p);
  static oopDesc* oop_load(oopDesc* base, volatile narrowOop* p) {
    return rt_load(base, p);
  }

  static oopDesc* oop_load_not_in_heap(volatile oop* p);
  static oopDesc* oop_load_not_in_heap(volatile narrowOop* p) {
    return rt_load_not_in_heap(p);
  }

  static void clone_post_barrier(oopDesc* array);

  static int oop_arraycopy_checkcast(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array);
  static int oop_arraycopy_checkcast(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array);
  static int oop_arraycopy_checkcast(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array) {
    return rt_arraycopy_checkcast(src_p, dst_p, length, dst_array);
  }

  static void oop_arraycopy_disjoint(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_disjoint(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_disjoint(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array) {
    rt_arraycopy_disjoint(src_p, dst_p, length, dst_array);
  }

  static void oop_arraycopy_conjoint(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_conjoint(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_conjoint(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array) {
    rt_arraycopy_conjoint(src_p, dst_p, length, dst_array);
  }
};

#endif // SHARE_GC_RTGC_RTGCBARRIER_HPP
