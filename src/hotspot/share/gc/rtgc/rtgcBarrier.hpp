
#ifndef SHARE_GC_RTGC_RTGCBARRIER_HPP
#define SHARE_GC_RTGC_RTGCBARRIER_HPP

#include "memory/allocation.hpp"
#include "oops/oop.hpp"

class RtgcBarrier : public AllStatic {
  static void (*rt_store)(narrowOop* p, oopDesc* new_value, oopDesc* base);
  static void (*rt_store_not_in_heap)(narrowOop* p, oopDesc* new_value);
  static void (*rt_store_not_in_heap_uninitialized)(narrowOop* p, oopDesc* new_value);

  static oopDesc* (*rt_xchg)(volatile narrowOop* p, oopDesc* new_value, oopDesc* base);
  static oopDesc* (*rt_xchg_not_in_heap)(volatile narrowOop* p, oopDesc* new_value);

  static oopDesc* (*rt_cmpxchg)(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static oopDesc* (*rt_cmpxchg_not_in_heap)(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value);

  static oopDesc* (*rt_load)(volatile narrowOop* p, oopDesc* base);
  static oopDesc* (*rt_load_not_in_heap)(volatile narrowOop* p);

  static int  (*rt_arraycopy_checkcast)(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_disjoint )(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_uninitialized)(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_conjoint )(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);

  static bool rt_cmpset(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static bool rt_cmpset_not_in_heap(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value);

public:
  static void init_barrier_runtime();

  static inline bool needBarrier(DecoratorSet decorators, ptrdiff_t offset = (ptrdiff_t)0xFFFF) {
    DecoratorSet no_barrier = AS_RAW | AS_NO_KEEPALIVE;
    return (no_barrier & decorators) == 0
        && offset > oopDesc::klass_offset_in_bytes();
  }

  static address getStoreFunction(DecoratorSet decorators);
  static address getXchgFunction(bool in_heap);
  static address getCmpSetFunction(bool in_heap);
  static address getLoadFunction(bool in_heap);
  static address getArrayCopyFunction(DecoratorSet decorators);

  static void oop_store(oop* p, oopDesc* new_value, oopDesc* base);
  static void oop_store(narrowOop* p, oopDesc* new_value, oopDesc* base) {
    rt_store(p, new_value, base);
  }

  static void oop_store_not_in_heap(oop* p, oopDesc* new_value);
  static void oop_store_not_in_heap(narrowOop* p, oopDesc* new_value) {
    rt_store_not_in_heap(p, new_value);
  }

  static void oop_store_not_in_heap_uninitialized(oop* p, oopDesc* new_value);
  static void oop_store_not_in_heap_uninitialized(narrowOop* p, oopDesc* new_value) {
    rt_store_not_in_heap_uninitialized(p, new_value);
  }

  static oopDesc* oop_xchg(volatile oop* p, oopDesc* new_value, oopDesc* base);
  static oopDesc* oop_xchg(volatile narrowOop* p, oopDesc* new_value, oopDesc* base) {
    return rt_xchg(p, new_value, base);
  }

  static oopDesc* oop_xchg_not_in_heap(volatile oop* p, oopDesc* new_value);
  static oopDesc* oop_xchg_not_in_heap(volatile narrowOop* p, oopDesc* new_value) {
    return rt_xchg_not_in_heap(p, new_value);
  }

  static oopDesc* oop_cmpxchg(volatile oop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static oopDesc* oop_cmpxchg(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
    return rt_cmpxchg(p, cmp_value, new_value, base);
  }

  static oopDesc* oop_cmpxchg_not_in_heap(volatile oop* p, oopDesc* cmp_value, oopDesc* new_value);
  static oopDesc* oop_cmpxchg_not_in_heap(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value) {
    return rt_cmpxchg_not_in_heap(p, cmp_value, new_value);
  }

  static oopDesc* oop_load(volatile oop* p, oopDesc* base);
  static oopDesc* oop_load(volatile narrowOop* p, oopDesc* base) {
    return rt_load(p, base);
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

  static void oop_arraycopy_uninitialized(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_uninitialized(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_uninitialized(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array) {
    rt_arraycopy_uninitialized(src_p, dst_p, length, dst_array);
  }

  static void oop_arraycopy_conjoint(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_conjoint(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array);
  static void oop_arraycopy_conjoint(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array) {
    rt_arraycopy_conjoint(src_p, dst_p, length, dst_array);
  }
};

#endif // SHARE_GC_RTGC_RTGCBARRIER_HPP
