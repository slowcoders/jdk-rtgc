#ifndef SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP
#define SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP

#include "gc/shared/accessBarrierSupport.inline.hpp"
#include "gc/rtgc/rtgcBarrier.inline.hpp"
#include "gc/rtgc/rtgcBarrierSet.hpp"
#include "utilities/debug.hpp"
#include "oops/accessBackend.hpp"

oop rtgc_break(const char* file, int line, const char* function);

#define RTGC_FAIL() rtgc_break(__FILE__, __LINE__, __FUNCTION__)

template <DecoratorSet decorators, typename BarrierSetT>
template <DecoratorSet expected>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::verify_decorators_present() {
  if ((decorators & expected) == 0) {
    fatal("Using unsupported access decorators");
  }
}

template <DecoratorSet decorators, typename BarrierSetT>
template <DecoratorSet expected>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::verify_decorators_absent() {
  if ((decorators & expected) != 0) {
    fatal("Using unsupported access decorators");
  }
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop* RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::field_addr(oop base, ptrdiff_t offset) {
  assert(base != NULL, "Invalid base");
  return reinterpret_cast<oop*>(reinterpret_cast<intptr_t>((void*)base) + offset);
}


//
// In heap
//
template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_in_heap(T* addr) {
  return RTGC_FAIL();
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_in_heap_at(oop base, ptrdiff_t offset) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);
  const oop o = RtgcBarrier::oop_load(base, addr);
  return o;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_in_heap(T* addr, oop compare_value, oop new_value) {
  return RTGC_FAIL();
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_in_heap_at(oop base, ptrdiff_t offset, oop compare_value, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF | ON_UNKNOWN_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();

  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);
  return RtgcBarrier::oop_cmpxchg(base, addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_in_heap(T* addr, oop new_value) {
  return RTGC_FAIL();
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_in_heap_at(oop base, ptrdiff_t offset, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();

  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline bool RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                                                                       arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                                                                       size_t length) {
  T* src = arrayOopDesc::obj_offset_to_raw(src_obj, src_offset_in_bytes, src_raw);
  T* dst = arrayOopDesc::obj_offset_to_raw(dst_obj, dst_offset_in_bytes, dst_raw);

  if (!HasDecorator<decorators, ARRAYCOPY_CHECKCAST>::value) {
    RtgcBarrier::oop_arraycopy_nocheck(src, dst, length, dst_obj);
    return true;
  }

  return RtgcBarrier::oop_arraycopy_checkcast(src, dst, length, dst_obj) == 0;
}

template <DecoratorSet decorators, typename BarrierSetT>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::clone_in_heap(oop src, oop dst, size_t size) {
  Raw::clone_in_heap(src, dst, size);
  RtgcBarrier::clone_post_barrier(dst);
}

//
// Not in heap
//
template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_not_in_heap(T* addr) {
  verify_decorators_absent<ON_UNKNOWN_OOP_REF>();

  const oop o = RtgcBarrier::oop_load_in_root(addr);
  return o;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_store_not_in_heap(T* addr, oop new_value) {
  verify_decorators_absent<ON_UNKNOWN_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();

  const oop o = RtgcBarrier::oop_xchg_in_root(addr, new_value);
  return o;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_not_in_heap(T* addr, oop compare_value, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();

  return RtgcBarrier::oop_cmpxchg_in_root(addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_not_in_heap(T* addr, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();

  return RtgcBarrier::oop_xchg_in_root(addr, new_value);
}

#endif // SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP
