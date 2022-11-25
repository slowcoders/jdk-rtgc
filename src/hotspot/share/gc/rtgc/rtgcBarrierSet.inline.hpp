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
  assert(RtgcBarrier::is_raw_access(decorators, false), "illegal access decorators");

  return ModRef::oop_load_in_heap(addr);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_load_in_heap_at(oop base, ptrdiff_t offset) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, base, offset, false)) {
    return ModRef::oop_load_in_heap(addr);
  }
  oop o;
  if (decorators & IS_ARRAY) {
    o = RtgcBarrier::oop_load_array_item(addr, base);
  } else {
    o = RtgcBarrier::oop_load(addr, base);
  }
  return o;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_store_in_heap(T* addr, oop value) {
  assert(RtgcBarrier::is_raw_access(decorators, true), "illegal access decorators");

  precond((decorators & IS_DEST_UNINITIALIZED) == 0);
  ModRef::oop_store_in_heap(addr, value);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_store_in_heap_at(oop base, ptrdiff_t offset, oop value) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, base, offset, true)) {
    ModRef::oop_store_in_heap(addr, value);
    return;
  }
  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());
  bs->template write_ref_field_pre<decorators>(addr);
  if (decorators & ON_UNKNOWN_OOP_REF) {
    RtgcBarrier::oop_store_unknown(addr, value, base);
  } else if (decorators & IS_ARRAY) {
    RtgcBarrier::oop_store_array_item(addr, value, base);
  } else {
    RtgcBarrier::oop_store(addr, value, base);
  }
  bs->template write_ref_field_post<decorators>(addr, value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_cmpxchg_in_heap(T* addr, oop compare_value, oop new_value) {
  assert(RtgcBarrier::is_raw_access(decorators, true), "illegal access decorators");

  return ModRef::oop_atomic_cmpxchg_in_heap(addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_cmpxchg_in_heap_at(oop base, ptrdiff_t offset, oop compare_value, oop new_value) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, base, offset, true)) {
    return ModRef::oop_atomic_cmpxchg_in_heap(addr, compare_value, new_value);
  }
  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());
  bs->template write_ref_field_pre<decorators>(addr);
  oop result;
  if (decorators & ON_UNKNOWN_OOP_REF) {
    result = RtgcBarrier::oop_cmpxchg_unknown(addr, compare_value, new_value, base);
  } else if (decorators & IS_ARRAY) {
    result = RtgcBarrier::oop_cmpxchg_array_item(addr, compare_value, new_value, base);
  } else {
    result = RtgcBarrier::oop_cmpxchg(addr, compare_value, new_value, base);
  }
  if (result == compare_value) {
    bs->template write_ref_field_post<decorators>(addr, new_value);
  }
  return result;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_xchg_in_heap(T* addr, oop new_value) {
  assert(RtgcBarrier::is_raw_access(decorators, true), "illegal access decorators");

  return ModRef::oop_atomic_xchg_in_heap(addr, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_xchg_in_heap_at(oop base, ptrdiff_t offset, oop new_value) {
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);

  if (!RtgcBarrier::needBarrier(decorators, base, offset, true)) {
    return ModRef::oop_atomic_xchg_in_heap(addr, new_value);
  }
  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());
  bs->template write_ref_field_pre<decorators>(addr);
  oop result;
  if (decorators & ON_UNKNOWN_OOP_REF) {
    result = RtgcBarrier::oop_xchg_unknown(addr, new_value, base);
  } else if (decorators & IS_ARRAY) {
    result = RtgcBarrier::oop_xchg_array_item(addr, new_value, base);
  } else {
    result = RtgcBarrier::oop_xchg(addr, new_value, base);
  }
  bs->template write_ref_field_post<decorators>(addr, new_value);
  return result;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline bool RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,                                                                                       arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                      size_t length) {
  bool need_barrier = RtgcBarrier::needBarrier(decorators, dst_obj, 0xFFFF, true);

  BarrierSetT *bs = barrier_set_cast<BarrierSetT>(barrier_set());

  src_raw = arrayOopDesc::obj_offset_to_raw(src_obj, src_offset_in_bytes, src_raw);
  dst_raw = arrayOopDesc::obj_offset_to_raw(dst_obj, dst_offset_in_bytes, dst_raw);

  if (!HasDecorator<decorators, ARRAYCOPY_CHECKCAST>::value) {
    if (!need_barrier) {
      return ModRef::oop_arraycopy(NULL, 0, src_raw, NULL, 0, dst_raw, length);
    } 
    bs->write_ref_array_pre(dst_raw, length,
                            HasDecorator<decorators, IS_DEST_UNINITIALIZED>::value);
    if (!HasDecorator<decorators, ARRAYCOPY_DISJOINT>::value) {
      RtgcBarrier::oop_arraycopy_conjoint(src_raw, dst_raw, length, dst_obj);
    } else if (HasDecorator<decorators, IS_DEST_UNINITIALIZED>::value) {
      RtgcBarrier::oop_arraycopy_uninitialized(src_raw, dst_raw, length, dst_obj);
    } else {
      RtgcBarrier::oop_arraycopy_disjoint(src_raw, dst_raw, length, dst_obj);
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
    if (!need_barrier) {
      ModRef::oop_arraycopy(NULL, 0, src_raw, NULL, 0, dst_raw, count);
    } else {
      RtgcBarrier::oop_arraycopy_disjoint(src_raw, dst_raw, count, dst_obj);
    }
    bs->write_ref_array((HeapWord*)dst_raw, count);
    return count == length;
  }
}

template <DecoratorSet decorators, typename BarrierSetT>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
clone_in_heap(oop src, oop dst, size_t size) {
  RtgcBarrier::oop_clone_in_heap(src, dst, size);
}

//
// Not in heap
//
template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_load_not_in_heap(T* addr) {
  if (RtgcBarrier::is_raw_access(decorators, false)) {
    return ModRef::oop_load_not_in_heap(addr);
  }
  const oop o = RtgcBarrier::oop_load_not_in_heap(addr);
  return o;
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_store_not_in_heap(T* addr, oop new_value) {
  if (RtgcBarrier::is_raw_access(decorators, true)) {
    ModRef::oop_store_not_in_heap(addr, new_value);
  }
  else if (decorators & IS_DEST_UNINITIALIZED) {
    RtgcBarrier::oop_store_not_in_heap_uninitialized(addr, new_value);
  }
  else {
    RtgcBarrier::oop_store_not_in_heap(addr, new_value);
  }
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_cmpxchg_not_in_heap(T* addr, oop compare_value, oop new_value) {
  if (RtgcBarrier::is_raw_access(decorators, true)) {
    return ModRef::oop_atomic_cmpxchg_not_in_heap(addr, compare_value, new_value);
  }
  return RtgcBarrier::oop_cmpxchg_not_in_heap(addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::
oop_atomic_xchg_not_in_heap(T* addr, oop new_value) {
  if (RtgcBarrier::is_raw_access(decorators, true)) {
    return ModRef::oop_atomic_xchg_not_in_heap(addr, new_value);
  }
  return RtgcBarrier::oop_xchg_not_in_heap(addr, new_value);
}

#endif // SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP
