
#ifndef SHARE_GC_RTGC_RTGCBARRIER_HPP
#define SHARE_GC_RTGC_RTGCBARRIER_HPP

#include "memory/allocation.hpp"
#include "oops/oop.hpp"
#include "gc/rtgc/rtgcHeap.hpp"

class RtgcBarrier : public AllStatic {
  static void (*rt_store)(void* p, oopDesc* new_value, oopDesc* base);
  static void (*rt_store_array_item)(void* p, oopDesc* new_value, oopDesc* base);
  static void (*rt_store_not_in_heap)(void* p, oopDesc* new_value);
  static void (*rt_store_not_in_heap_uninitialized)(void* p, oopDesc* new_value);

  static oopDesc* (*rt_xchg)(volatile void* p, oopDesc* new_value, oopDesc* base);
  static oopDesc* (*rt_xchg_array_item)(volatile void* p, oopDesc* new_value, oopDesc* base);
  static oopDesc* (*rt_xchg_not_in_heap)(volatile void* p, oopDesc* new_value);

  static oopDesc* (*rt_cmpxchg)(volatile void* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static oopDesc* (*rt_cmpxchg_array_item)(volatile void* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static oopDesc* (*rt_cmpxchg_not_in_heap)(volatile void* p, oopDesc* cmp_value, oopDesc* new_value);

  static oopDesc* (*rt_load)(volatile void* p, oopDesc* base);
  static oopDesc* (*rt_load_array_item)(volatile void* p, oopDesc* base);
  static oopDesc* (*rt_load_not_in_heap)(volatile void* p);

  static int  (*rt_arraycopy_checkcast)(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_disjoint )(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_uninitialized)(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);
  static void (*rt_arraycopy_conjoint )(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);

  static bool rt_cmpset(volatile void* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static bool rt_cmpset_not_in_heap(volatile void* p, oopDesc* cmp_value, oopDesc* new_value);
  static bool rt_cmpset_unknown(volatile void* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);

  template<DecoratorSet decorators, typename T> 
  static oopDesc* rt_load_c1(T* addr, oopDesc* base);

  template<DecoratorSet decorators, typename T> 
  static void rt_store_c1(T* addr, oopDesc* new_value, oopDesc* base);

  template<DecoratorSet decorators, typename T> 
  static oopDesc* rt_xchg_c1(T* addr, oopDesc* new_value, oopDesc* base);

  template<DecoratorSet decorators, typename T> 
  static oopDesc* rt_cmpset_c1(T* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);

public:
  static void init_barrier_runtime();

  static inline bool is_raw_access(DecoratorSet decorators, bool op_store = true) {
    DecoratorSet no_barrier = AS_RAW | AS_NO_KEEPALIVE;
    if (op_store && !RtLazyClearWeakHandle) {
      no_barrier |= ON_PHANTOM_OOP_REF;
    }
    return (no_barrier & decorators) != 0;
  }

  static inline bool needBarrier(DecoratorSet decorators, oopDesc* base,
                                 ptrdiff_t offset, bool op_store) {
    precond(!(decorators & IS_ARRAY) ||
            (!(decorators & (IS_DEST_UNINITIALIZED|IN_NATIVE)) && (decorators & IN_HEAP)));
    return !is_raw_access(decorators, op_store)
        && offset > oopDesc::klass_offset_in_bytes()
        && rtHeap::is_trackable(base);
  }

  static address getStoreFunction(DecoratorSet decorators);
  static address getXchgFunction(DecoratorSet decorators);
  static address getCmpSetFunction(DecoratorSet decorators);
  static address getLoadFunction(DecoratorSet decorators);
  static address getArrayCopyFunction(DecoratorSet decorators);

  static void oop_store(oop* p, oopDesc* new_value, oopDesc* base);
  static void oop_store(narrowOop* p, oopDesc* new_value, oopDesc* base) {
    rt_store(p, new_value, base);
  }

  static void oop_store_array_item(oop* p, oopDesc* new_value, oopDesc* base);
  static void oop_store_array_item(narrowOop* p, oopDesc* new_value, oopDesc* base) {
    rt_store_array_item(p, new_value, base);
  }

  static void oop_store_unknown(void* p, oopDesc* new_value, oopDesc* base);

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

  static oopDesc* oop_xchg_array_item(volatile oop* p, oopDesc* new_value, oopDesc* base);
  static oopDesc* oop_xchg_array_item(volatile narrowOop* p, oopDesc* new_value, oopDesc* base) {
    return rt_xchg_array_item(p, new_value, base);
  }

  static oopDesc* oop_xchg_unknown(volatile void* p, oopDesc* new_value, oopDesc* base);

  static oopDesc* oop_xchg_not_in_heap(volatile oop* p, oopDesc* new_value);
  static oopDesc* oop_xchg_not_in_heap(volatile narrowOop* p, oopDesc* new_value) {
    return rt_xchg_not_in_heap(p, new_value);
  }

  static oopDesc* oop_cmpxchg(volatile oop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static oopDesc* oop_cmpxchg(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
    return rt_cmpxchg(p, cmp_value, new_value, base);
  }

  static oopDesc* oop_cmpxchg_array_item(volatile oop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
  static oopDesc* oop_cmpxchg_array_item(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
    return rt_cmpxchg_array_item(p, cmp_value, new_value, base);
  }

  static oopDesc* oop_cmpxchg_unknown(volatile void* p, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);

  static oopDesc* oop_cmpxchg_not_in_heap(volatile oop* p, oopDesc* cmp_value, oopDesc* new_value);
  static oopDesc* oop_cmpxchg_not_in_heap(volatile narrowOop* p, oopDesc* cmp_value, oopDesc* new_value) {
    return rt_cmpxchg_not_in_heap(p, cmp_value, new_value);
  }

  static oopDesc* oop_load(volatile oop* p, oopDesc* base);
  static oopDesc* oop_load(volatile narrowOop* p, oopDesc* base) {
    return rt_load(p, base);
  }

  static oopDesc* oop_load_array_item(volatile oop* p, oopDesc* base);
  static oopDesc* oop_load_array_item(volatile narrowOop* p, oopDesc* base) {
    return rt_load_array_item(p, base);
  }

  static oopDesc* oop_load_unknown(volatile void* p, oopDesc* base);

  static oopDesc* oop_load_not_in_heap(volatile oop* p);
  static oopDesc* oop_load_not_in_heap(volatile narrowOop* p) {
    return rt_load_not_in_heap(p);
  }

  static void oop_clone_in_heap(oop src, oop dst, size_t size);

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
