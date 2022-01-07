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

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::load_barrier_on_oop_field_preloaded(T* addr, oop o) {
  verify_decorators_absent<ON_UNKNOWN_OOP_REF>();
  return o;
  // if (HasDecorator<decorators, AS_NO_KEEPALIVE>::value) {
  //   if (HasDecorator<decorators, ON_STRONG_OOP_REF>::value) {
  //     return ZBarrier::weak_load_barrier_on_oop_field_preloaded(addr, o);
  //   } else if (HasDecorator<decorators, ON_WEAK_OOP_REF>::value) {
  //     return ZBarrier::weak_load_barrier_on_weak_oop_field_preloaded(addr, o);
  //   } else {
  //     assert((HasDecorator<decorators, ON_PHANTOM_OOP_REF>::value), "Must be");
  //     return ZBarrier::weak_load_barrier_on_phantom_oop_field_preloaded(addr, o);
  //   }
  // } else {
  //   if (HasDecorator<decorators, ON_STRONG_OOP_REF>::value) {
  //     return ZBarrier::load_barrier_on_oop_field_preloaded(addr, o);
  //   } else if (HasDecorator<decorators, ON_WEAK_OOP_REF>::value) {
  //     return ZBarrier::load_barrier_on_weak_oop_field_preloaded(addr, o);
  //   } else {
  //     assert((HasDecorator<decorators, ON_PHANTOM_OOP_REF>::value), "Must be");
  //     return ZBarrier::load_barrier_on_phantom_oop_field_preloaded(addr, o);
  //   }
  // }
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::load_barrier_on_unknown_oop_field_preloaded(oop base, ptrdiff_t offset, T* addr, oop o) {
  verify_decorators_present<ON_UNKNOWN_OOP_REF>();
  return o;
  // const DecoratorSet decorators_known_strength =
  //   AccessBarrierSupport::resolve_possibly_unknown_oop_ref_strength<decorators>(base, offset);

  // if (HasDecorator<decorators, AS_NO_KEEPALIVE>::value) {
  //   if (decorators_known_strength & ON_STRONG_OOP_REF) {
  //     return ZBarrier::weak_load_barrier_on_oop_field_preloaded(addr, o);
  //   } else if (decorators_known_strength & ON_WEAK_OOP_REF) {
  //     return ZBarrier::weak_load_barrier_on_weak_oop_field_preloaded(addr, o);
  //   } else {
  //     assert(decorators_known_strength & ON_PHANTOM_OOP_REF, "Must be");
  //     return ZBarrier::weak_load_barrier_on_phantom_oop_field_preloaded(addr, o);
  //   }
  // } else {
  //   if (decorators_known_strength & ON_STRONG_OOP_REF) {
  //     return ZBarrier::load_barrier_on_oop_field_preloaded(addr, o);
  //   } else if (decorators_known_strength & ON_WEAK_OOP_REF) {
  //     return ZBarrier::load_barrier_on_weak_oop_field_preloaded(addr, o);
  //   } else {
  //     assert(decorators_known_strength & ON_PHANTOM_OOP_REF, "Must be");
  //     return ZBarrier::load_barrier_on_phantom_oop_field_preloaded(addr, o);
  //   }
  // }
}

//
// In heap
//
template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_in_heap(T* addr) {
  verify_decorators_absent<ON_UNKNOWN_OOP_REF>();
  return RTGC_FAIL();
  // const oop o = Raw::oop_load_in_heap(addr);
  // return load_barrier_on_oop_field_preloaded(addr, o);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_in_heap_at(oop base, ptrdiff_t offset) {
  // oop* const addr = field_addr(base, offset);
  const oop o = Raw::oop_load_in_heap_at(base, offset);
  return o;
  // if (HasDecorator<decorators, ON_UNKNOWN_OOP_REF>::value) {
  //   return load_barrier_on_unknown_oop_field_preloaded(base, offset, addr, o);
  // }
  // return load_barrier_on_oop_field_preloaded(addr, o);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_in_heap(T* addr, oop compare_value, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();
  return RTGC_FAIL();
  // ZBarrier::load_barrier_on_oop_field(addr);
  // return Raw::oop_atomic_cmpxchg_in_heap(addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_cmpxchg_in_heap_at(oop base, ptrdiff_t offset, oop compare_value, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF | ON_UNKNOWN_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);
  return RtgcBarrier::oop_cmpxchg(base, addr, compare_value, new_value);
  // // Through Unsafe.CompareAndExchangeObject()/CompareAndSetObject() we can receive
  // // calls with ON_UNKNOWN_OOP_REF set. However, we treat these as ON_STRONG_OOP_REF,
  // // with the motivation that if you're doing Unsafe operations on a Reference.referent
  // // field, then you're on your own anyway.
  // ZBarrier::load_barrier_on_oop_field(field_addr(base, offset));
  // return Raw::oop_atomic_cmpxchg_in_heap_at(base, offset, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_in_heap(T* addr, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();
  return RTGC_FAIL();
  //const oop o = Raw::oop_atomic_xchg_in_heap(addr, new_value);
  //return ZBarrier::load_barrier_on_oop(o);
}

template <DecoratorSet decorators, typename BarrierSetT>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_in_heap_at(oop base, ptrdiff_t offset, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();
  typedef typename HeapOopType<decorators>::type oop_type;
  oop_type* addr = AccessInternal::oop_field_addr<decorators>(base, offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
  // const oop o = Raw::oop_atomic_xchg_in_heap_at(base, offset, new_value);
  // return ZBarrier::load_barrier_on_oop(o);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline bool RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                                                                       arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                                                                       size_t length) {
  T* src = arrayOopDesc::obj_offset_to_raw(src_obj, src_offset_in_bytes, src_raw);
  T* dst = arrayOopDesc::obj_offset_to_raw(dst_obj, dst_offset_in_bytes, dst_raw);

  if (!HasDecorator<decorators, ARRAYCOPY_CHECKCAST>::value) {
    RtgcBarrier::oop_arraycopy_nocheck(dst_obj, dst, src, length);
    return true;
    // No check cast, bulk barrier and bulk copy
    // ZBarrier::load_barrier_on_oop_array(src, length);
    //return Raw::oop_arraycopy_in_heap(NULL, 0, src, NULL, 0, dst, length);
  }

  return RtgcBarrier::oop_arraycopy_checkcast(dst_obj, dst, src, length);

  // Check cast and copy each elements
  // Klass* const dst_klass = objArrayOop(dst_obj)->element_klass();
  // for (const T* const end = src + length; src < end; src++, dst++) {
  //   const oop elem = ZBarrier::load_barrier_on_oop_field(src);
  //   if (!oopDesc::is_instanceof_or_null(elem, dst_klass)) {
  //     // Check cast failed
  //     return false;
  //   }

  //   // Cast is safe, since we know it's never a narrowOop
  //   *(oop*)dst = elem;
  // }

  // return true;
}

template <DecoratorSet decorators, typename BarrierSetT>
inline void RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::clone_in_heap(oop src, oop dst, size_t size) {
  // ZBarrier::load_barrier_on_oop_fields(src);
  Raw::clone_in_heap(src, dst, size);
  RtgcBarrier::clone_post_barrier(dst);
  // bool locked = RtgcBarrier::lock_refLink(dst);
  // RTGC_CloneClosure c(dst);
  // dst->oop_iterate(&c);
  // RtgcBarrier::unlock_refLink(locked);
}

//
// Not in heap
//
template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_load_not_in_heap(T* addr) {
  verify_decorators_absent<ON_UNKNOWN_OOP_REF>();

  const oop o = Raw::oop_load_not_in_heap(addr);
  return o;
  // return load_barrier_on_oop_field_preloaded(addr, o);
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
  //return Raw::oop_atomic_cmpxchg_not_in_heap(addr, compare_value, new_value);
}

template <DecoratorSet decorators, typename BarrierSetT>
template <typename T>
inline oop RtgcBarrierSet::AccessBarrier<decorators, BarrierSetT>::oop_atomic_xchg_not_in_heap(T* addr, oop new_value) {
  verify_decorators_present<ON_STRONG_OOP_REF>();
  verify_decorators_absent<AS_NO_KEEPALIVE>();

  return RtgcBarrier::oop_xchg_in_root(addr, new_value);
  //return Raw::oop_atomic_xchg_not_in_heap(addr, new_value);
}

#endif // SHARE_GC_RTGC_RTGCBARRIERSET_INLINE_HPP
