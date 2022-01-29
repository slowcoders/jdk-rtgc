#ifndef SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP
#define SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP

#include "gc/rtgc/rtgcBarrier.inline.hpp"
#include "gc/rtgc/rtgcBarrierSet.hpp"
#include "oops/compressedOops.inline.hpp"
#include "oops/klass.inline.hpp"
#include "oops/objArrayOop.hpp"
#include "oops/oop.hpp"

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_in_heap(T* addr) {
  assert(!RtgcBarrier::needBarrier(decorators), "illegal access decorators");

  return Raw::oop_load_in_heap(addr);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_load_in_heap_at(oop base, ptrdiff_t offset) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    return oop_load_in_heap(addr);
  }
  const oop o = RtgcBarrier::oop_load(addr, base);
  return o;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_store_in_heap(T* addr, oop value) {
  assert(!RtgcBarrier::needBarrier(decorators), "illegal access decorators");

  Raw::oop_store_in_heap(addr, value);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_store_in_heap_at(oop base, ptrdiff_t offset, oop value) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    oop_store_in_heap(addr, value);
    return;
  }
  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());
  bs->template write_ref_field_pre<decorators>(addr);
  RtgcBarrier::oop_store(addr, value, base);
  bs->template write_ref_field_post<decorators>(addr, value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_cmpxchg_in_heap(T* addr, oop compare_value, oop new_value) {
  assert(!RtgcBarrier::needBarrier(decorators), "illegal access decorators");

  return Raw::oop_atomic_cmpxchg_in_heap(addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_cmpxchg_in_heap_at(oop base, ptrdiff_t offset, oop compare_value, oop new_value) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    return oop_atomic_cmpxchg_in_heap(addr, compare_value, new_value);
  }
  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());
  bs->template write_ref_field_pre<decorators>(addr);
  oop result = RtgcBarrier::oop_cmpxchg(addr, compare_value, new_value, base);
  if (result == compare_value) {
    bs->template write_ref_field_post<decorators>(addr, new_value);
  }
  return result;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_xchg_in_heap(T* addr, oop new_value) {
  assert(!RtgcBarrier::needBarrier(decorators), "illegal access decorators");

  return Raw::oop_atomic_xchg_in_heap(addr, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_xchg_in_heap_at(oop base, ptrdiff_t offset, oop new_value) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    return oop_atomic_cmpxchg_in_heap(addr, new_value);
  }
  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());
  bs->template write_ref_field_pre<decorators>(addr);
  oop result = RtgcBarrier::oop_xchg(addr, new_value, base);
  bs->template write_ref_field_post<decorators>(addr, new_value);
  return result;
}



template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline bool RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,                                                                                       arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                      size_t length) {
  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());

  src_raw = arrayOopDesc::obj_offset_to_raw(src_obj, src_offset_in_bytes, src_raw);
  dst_raw = arrayOopDesc::obj_offset_to_raw(dst_obj, dst_offset_in_bytes, dst_raw);

  if (!HasDecorator<decorators, ARRAYCOPY_CHECKCAST>::value) {
    bs->write_ref_array_pre(dst_raw, length,
                            HasDecorator<decorators, IS_DEST_UNINITIALIZED>::value);
    if (HasDecorator<decorators, ARRAYCOPY_DISJOINT>::value) {
      RtgcBarrier::oop_arraycopy_disjoint(src_raw, dst_raw, length, dst_obj);
    } else {
      RtgcBarrier::oop_arraycopy_conjoint(src_raw, dst_raw, length, dst_obj);
    }
    bs->write_ref_array((HeapWord*)dst_raw, length);
    return true;
  } else {
    assert(dst_obj != NULL, "better have an actual oop");
    Klass* bound = objArrayOop(dst_obj)->element_klass();
    T* from = const_cast<T*>(src_raw);
    size_t count = 0;
    for (; count < length; count++) {
      T element = from[count];
      if (!oopDesc::is_instanceof_or_null(CompressedOops::decode(element), bound)) {
        break;
      }
    }
    bs->write_ref_array_pre(dst_raw, count, 
                            HasDecorator<decorators, IS_DEST_UNINITIALIZED>::value);;
    RtgcBarrier::oop_arraycopy_disjoint(src_raw, dst_raw, count, dst_obj);
    bs->write_ref_array((HeapWord*)dst_raw, count);
    return count == length;
  }
}

template <DecoratorSet decorators, typename BarrierSetT>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
clone_in_heap(oop src, oop dst, size_t size) {
  Raw::clone_in_heap(src, dst, size);
  RtgcBarrier::clone_post_barrier(dst);
  //BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());
  //bs->write_region(MemRegion((HeapWord*)(void*)dst, size));
}

//
// Not in heap
//
template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_not_in_heap(T* addr) {
  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    return Raw::oop_load_not_in_heap(addr);
  }
  const oop o = RtgcBarrier::oop_load_not_in_heap(addr);
  return o;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_store_not_in_heap(T* addr, oop new_value) {
  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    Raw::oop_store_not_in_heap(addr, new_value);
  }
  else {
    RtgcBarrier::oop_store_not_in_heap(addr, new_value);
  }
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_not_in_heap(T* addr, oop compare_value, oop new_value) {
  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    return Raw::oop_atomic_cmpxchg_not_in_heap(addr, compare_value, new_value);
  }
  return RtgcBarrier::oop_cmpxchg_not_in_heap(addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_not_in_heap(T* addr, oop new_value) {
  if (!RtgcBarrier::needBarrier(decorators, offset)) {
    return Raw::oop_atomic_xchg_not_in_heap(addr, new_value);
  }
  return RtgcBarrier::oop_xchg_not_in_heap(addr, new_value);
}

#endif // SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP
