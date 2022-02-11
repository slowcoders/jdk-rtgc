#include "precompiled.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "oops/klass.hpp"
#include "oops/method.inline.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oopsHierarchy.hpp"
#include "oops/oop.inline.hpp"
#include "oops/accessDecorators.hpp"
#include "oops/compressedOops.hpp"
#include "memory/iterator.inline.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/rtgcDebug.hpp"

volatile int LOG_VERBOSE = 0;
static void* _base = 0;
static int _shift = 0;
static const int MAX_OBJ_SIZE = 256*1024*1024;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_BARRIER, function);
}

static void check_field_addr(void* base, volatile void* addr) {
  assert(addr > (address)base + oopDesc::klass_offset_in_bytes()
      , "invalid field addr");
  assert(addr < (address)base + MAX_OBJ_SIZE, "invalid field addr");
}

int rtgc_getOopShift() {
#ifdef _LP64
  if (UseCompressedOops) {
    if (_base != CompressedOops::base()) {
      _base = CompressedOops::base();
      //rtgc_log(LOG_OPT(1), "## narrowOop base=%p\n", _base);
    }
    if (_shift != CompressedOops::shift()) {
      _shift = CompressedOops::shift();
      if (_shift != 0 && _shift != 3) {
        //rtgc_log(LOG_OPT(1), "## narrowOop shift=%d\n", _shift);
      }
    }
    //assert(0 == CompressedOops::base(), "invalid narrowOop base");
    // assert(3 == CompressedOops::shift()
    //     || 0 == CompressedOops::shift(), "invalid narrowOop shift");
    return 0;//CompressedOops::shift();
  }
  else {
    return 8;
  }
#else
  return 4;
#endif
}

static void rtgc_set_volatile_field(volatile narrowOop* addr, oopDesc* value) {
  Atomic::release_store(addr, CompressedOops::encode(value));
}
static void rtgc_set_volatile_field(volatile oop* addr, oopDesc* value) {
  Atomic::release_store((volatile oopDesc**)addr, value);
}

static void rtgc_set_field(narrowOop* addr, oopDesc* value) {
  RawAccess<>::oop_store(addr, value);
}
static void rtgc_set_field(oop* addr, oopDesc* value) {
  RawAccess<>::oop_store(addr, value);
}


template<class T, bool inHeap, int shift>
void rtgc_store(T* addr, oopDesc* new_value, oopDesc* base) {
  check_field_addr(base, addr);
  rtgc_log(LOG_OPT(2), "store %p(%p) := %p\n", base, addr, new_value);
  RTGC::publish_and_lock_heap(new_value, base);
  oopDesc* old = CompressedOops::decode(*addr);
  rtgc_set_field(addr, new_value);
  if (new_value != NULL) RTGC::add_referrer(new_value, base);
  if (old != NULL) RTGC::remove_referrer(old, base);
  RTGC::unlock_heap(true);
}

template<class T, bool dest_uninitialized, int shift>
void rtgc_store_not_in_heap(T* addr, oopDesc* new_value) {
  rtgc_log(LOG_OPT(2), "store_nh (%p) := %p\n", addr, new_value);
  RTGC::publish_and_lock_heap(new_value);
  oop old = RawAccess<>::oop_load(addr);
  rtgc_set_volatile_field(addr, new_value);
  if (new_value != NULL) RTGC::add_global_reference(new_value);
  if (!dest_uninitialized && old != NULL) RTGC::remove_global_reference(old);
  RTGC::unlock_heap(true);
}

void (*RtgcBarrier::rt_store)(narrowOop* addr, oopDesc* new_value, oopDesc* base) = 0;
void RtgcBarrier::oop_store(oop* addr, oopDesc* new_value, oopDesc* base) {
  rtgc_store<oop, true, 0>(addr, new_value, base);
}

void (*RtgcBarrier::rt_store_not_in_heap)(narrowOop* addr, oopDesc* new_value);
void RtgcBarrier::oop_store_not_in_heap(oop* addr, oopDesc* new_value) {
  rtgc_store_not_in_heap<oop, false, 0>(addr, new_value);
}

void (*RtgcBarrier::rt_store_not_in_heap_uninitialized)(narrowOop* addr, oopDesc* new_value);
void RtgcBarrier::oop_store_not_in_heap_uninitialized(oop* addr, oopDesc* new_value) {
  rtgc_store_not_in_heap<oop, true, 0>(addr, new_value);
}

address RtgcBarrier::getStoreFunction(DecoratorSet decorators) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  precond(!in_heap || !uninitialized);
  return in_heap
     ? reinterpret_cast<address>(rt_store)
     : uninitialized
        ? reinterpret_cast<address>(rt_store_not_in_heap_uninitialized)
        : reinterpret_cast<address>(rt_store_not_in_heap);
}


template<class T, bool inHeap, int shift>
oopDesc* rtgc_xchg(volatile T* addr, oopDesc* new_value, oopDesc* base) {
  rtgc_log(LOG_OPT(3), "xchg %p(%p) <-> %p\n", base, addr, new_value);
  check_field_addr(base, addr);
  RTGC::publish_and_lock_heap(new_value, base);
  oop old = RawAccess<>::oop_load(addr);
  rtgc_set_volatile_field(addr, new_value);
  if (new_value != NULL) RTGC::add_referrer(new_value, base);
  if (old != NULL) RTGC::remove_referrer(old, base);
  RTGC::unlock_heap(true);
  return old;
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_xchg_not_in_heap(volatile T* addr, oopDesc* new_value) {
  rtgc_log(LOG_OPT(3), "xchg_nh (%p) <-> %p\n", addr, new_value);
  RTGC::publish_and_lock_heap(new_value);
  oop old = RawAccess<>::oop_load(addr);
  rtgc_set_volatile_field(addr, new_value);
  if (new_value != NULL) RTGC::add_global_reference(new_value);
  if (old != NULL) RTGC::remove_global_reference(old);
  RTGC::unlock_heap(true);
  return old;
}

oopDesc* (*RtgcBarrier::rt_xchg)(volatile narrowOop* addr, oopDesc* new_value, oopDesc* base);
oopDesc* RtgcBarrier::oop_xchg(volatile oop* addr, oopDesc* new_value, oopDesc* base) {
  return rtgc_xchg<oop, true, 0>(addr, new_value, base);
}

oopDesc* (*RtgcBarrier::rt_xchg_not_in_heap)(volatile narrowOop* addr, oopDesc* new_value);
oopDesc* RtgcBarrier::oop_xchg_not_in_heap(volatile oop* addr, oopDesc* new_value) {
  return rtgc_xchg_not_in_heap<oop, false, 0>(addr, new_value);
}

address RtgcBarrier::getXchgFunction(bool in_heap) {
  return in_heap ? reinterpret_cast<address>(rt_xchg)
                 : reinterpret_cast<address>(rt_xchg_not_in_heap);
}


template<class T, bool inHeap, int shift>
oopDesc* rtgc_cmpxchg(volatile T* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
  check_field_addr(base, addr);
  RTGC::publish_and_lock_heap(new_value, base);
  oop old = RawAccess<>::oop_load(addr);
  if ((oopDesc*)old == cmp_value) {
    rtgc_set_volatile_field(addr, new_value);
    if (new_value != NULL) RTGC::add_referrer(new_value, base);
    if (old != NULL) RTGC::remove_referrer(old, base);
  }
  RTGC::unlock_heap(true);
  rtgc_log(LOG_OPT(8), "cmpxchg %p(%p)=%p <-> %p r=%d\n", 
        base, addr, cmp_value, new_value, (oopDesc*)old == cmp_value);
  return old;
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_cmpxchg_not_in_heap(volatile T* addr, oopDesc* cmp_value, oopDesc* new_value) {
  RTGC::publish_and_lock_heap(new_value);
  oop old = RawAccess<>::oop_load(addr);
  if ((oopDesc*)old == cmp_value) {
    rtgc_set_volatile_field(addr, new_value);
    if (new_value != NULL) RTGC::add_global_reference(new_value);
    if (old != NULL) RTGC::remove_global_reference(old);
  }
  RTGC::unlock_heap(true);
  rtgc_log(LOG_OPT(8), "cmpxchg_nh (%p)=%p <-> %p r=%d\n", 
        addr, cmp_value, new_value, (oopDesc*)old == cmp_value);
  return old;
}

oopDesc* (*RtgcBarrier::rt_cmpxchg)(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base);
oopDesc* RtgcBarrier::oop_cmpxchg(volatile oop* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
  return rtgc_cmpxchg<oop, true, 0>(addr, cmp_value, new_value, base);
}

oopDesc* (*RtgcBarrier::rt_cmpxchg_not_in_heap)(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value);
oopDesc* RtgcBarrier::oop_cmpxchg_not_in_heap(volatile oop* addr, oopDesc* cmp_value, oopDesc* new_value) {
  return rtgc_cmpxchg_not_in_heap<oop, false, 0>(addr, cmp_value, new_value);
}

bool RtgcBarrier::rt_cmpset(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
  return rt_cmpxchg(addr, cmp_value, new_value, base) == cmp_value;
}
bool RtgcBarrier::rt_cmpset_not_in_heap(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value) {
  return rt_cmpxchg_not_in_heap(addr, cmp_value, new_value) == cmp_value;
}

address RtgcBarrier::getCmpSetFunction(bool in_heap) {
  return in_heap ? reinterpret_cast<address>(rt_cmpset)
                 : reinterpret_cast<address>(rt_cmpset_not_in_heap);
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_load(volatile T* addr, oopDesc* base) {
  check_field_addr(base, addr);
  //rtgc_log(LOG_OPT(4), "load %p(%p)\n", base, addr);
  // bool locked = RTGC::lock_heap();
  oop value = RawAccess<>::oop_load(addr);
  // rtgc_set_volatile_field(addr, new_value);
  // RTGC::unlock_heap(true);
  return value;
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_load_not_in_heap(volatile T* addr) {
  //rtgc_log(LOG_OPT(4), "load__ (%p)\n", addr);
  // bool locked = RTGC::lock_heap();
  oop value = RawAccess<>::oop_load(addr);
  // rtgc_set_volatile_field(addr, new_value);
  // RTGC::unlock_heap(true);
  return value;
}

oopDesc* (*RtgcBarrier::rt_load)(volatile narrowOop* addr, oopDesc* base) = 0;
oopDesc* RtgcBarrier::oop_load(volatile oop* addr, oopDesc* base) {
  return rtgc_load<oop, true, 0>(addr, base);
}
oopDesc* (*RtgcBarrier::rt_load_not_in_heap)(volatile narrowOop* addr);
oopDesc* RtgcBarrier::oop_load_not_in_heap(volatile oop* addr) {
  return rtgc_load_not_in_heap<oop, false, 0>(addr);
}

address RtgcBarrier::getLoadFunction(bool in_heap) {
  return in_heap ? reinterpret_cast<address>(rt_load)
                 : reinterpret_cast<address>(rt_load_not_in_heap);
}

//ObjArrayKlass::do_copy -> AccessBarrier::arraycopy_in_heap -> rtgc_arraycopy
template <class ITEM_T, DecoratorSet ds, int shift>
static int rtgc_arraycopy(ITEM_T* src_p, ITEM_T* dst_p, 
                    size_t length, arrayOopDesc* dst_array) {
  //LOG_OPT(5)                    
  // rtgc_log(!RTGC::debugOptions->opt1, "arraycopy (%p)->%p(%p): %d)\n", src_p, dst_array, dst_p, (int)length);
  bool checkcast = ARRAYCOPY_CHECKCAST & ds;
  bool dest_uninitialized = IS_DEST_UNINITIALIZED & ds;
  Klass* bound = !checkcast ? NULL
                            : ObjArrayKlass::cast(dst_array->klass())->element_klass();
  RTGC::lock_heap();                          
  for (size_t i = 0; i < length; i++) {
    ITEM_T s_raw = src_p[i]; 
  // for (ITEM_T* end_p = src_p + length; src_p < end_p; src_p ++, dst_p ++) {
  //   ITEM_T s_raw = src_p[0]; 
    oopDesc* item = CompressedOops::decode(s_raw);
    if (checkcast && item != NULL) {
      Klass* stype = item->klass();
      if (stype != bound && !stype->is_subtype_of(bound)) {
        memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*i);
        RTGC::unlock_heap(true);
        return i;//(end_p - src_p); // returns remain_count;
      }
    }
    oopDesc* old = CompressedOops::decode(dst_p[i]);
    // 사용불가 memmove 필요
    // dst_p[i] = s_raw;
    if (item != NULL) RTGC::add_referrer(item, dst_array);
    if (!dest_uninitialized && old != NULL) RTGC::remove_referrer(old, dst_array);
  } 
  memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*length);
  RTGC::unlock_heap(true);
  //rtgc_log(!RTGC::debugOptions->opt1, "arraycopy done (%p)->%p(%p): %d)\n", src_p, dst_array, dst_p, (int)length);
  return 0;
}

int (*RtgcBarrier::rt_arraycopy_checkcast)(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
int RtgcBarrier::oop_arraycopy_checkcast(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  return rtgc_arraycopy<oop, ARRAYCOPY_CHECKCAST, 0>(src_p, dst_p, length, dst_array);
}
int RtgcBarrier::oop_arraycopy_checkcast(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  assert(false, "not implemented");
  return false;
}

void (*RtgcBarrier::rt_arraycopy_disjoint)(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
void RtgcBarrier::oop_arraycopy_disjoint(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  rtgc_arraycopy<oop, ARRAYCOPY_DISJOINT, 0>(src_p, dst_p, length, dst_array);
}
void RtgcBarrier::oop_arraycopy_disjoint(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  fatal("not implemented");
}

void (*RtgcBarrier::rt_arraycopy_uninitialized)(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
void RtgcBarrier::oop_arraycopy_uninitialized(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  rtgc_arraycopy<oop, ARRAYCOPY_DISJOINT, 0>(src_p, dst_p, length, dst_array);
}
void RtgcBarrier::oop_arraycopy_uninitialized(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  fatal("not implemented");
}

void (*RtgcBarrier::rt_arraycopy_conjoint)(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array);
void RtgcBarrier::oop_arraycopy_conjoint(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  rtgc_arraycopy<oop, 0, 0>(src_p, dst_p, length, dst_array);
}
void RtgcBarrier::oop_arraycopy_conjoint(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  fatal("not implemented");
}

address RtgcBarrier::getArrayCopyFunction(DecoratorSet decorators) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;

  if (checkcast) {
    return reinterpret_cast<address>(rt_arraycopy_checkcast);
  } else if (dest_uninitialized) {
    return reinterpret_cast<address>(rt_arraycopy_uninitialized);
  } else if (disjoint) {
    return reinterpret_cast<address>(rt_arraycopy_disjoint);
  } else {
    return reinterpret_cast<address>(rt_arraycopy_conjoint);
  }
}

class RTGC_CloneClosure : public BasicOopIterateClosure {
  oopDesc* _rookie;
  void do_work(oopDesc* ref){
    if (ref != NULL) RTGC::add_referrer(ref, _rookie);
  }

public:
  RTGC_CloneClosure(oopDesc* rookie) { this->_rookie = rookie; }

  virtual void do_oop(narrowOop* p) { do_work(CompressedOops::decode(*p)); }
  virtual void do_oop(oop*       p) { do_work(*p); }
};

void RtgcBarrier::clone_post_barrier(oopDesc*  new_array) {
  RTGC::lock_heap();
  RTGC_CloneClosure c(new_array);
  new_array->oop_iterate(&c);
  RTGC::unlock_heap(true);
}


#define INIT_BARRIER_METHODS(T, shift) \
  *(void**)&rt_store = reinterpret_cast<void*>(rtgc_store<T, true, shift>); \
  *(void**)&rt_xchg = reinterpret_cast<void*>(rtgc_xchg<T, true, shift>); \
  *(void**)&rt_cmpxchg = reinterpret_cast<void*>(rtgc_cmpxchg<T, true, shift>); \
  *(void**)&rt_load = reinterpret_cast<void*>(rtgc_load<T, true, shift>); \
  *(void**)&rt_store_not_in_heap = reinterpret_cast<void*>(rtgc_store_not_in_heap<T, false, shift>); \
  *(void**)&rt_store_not_in_heap_uninitialized = reinterpret_cast<void*>(rtgc_store_not_in_heap<T, true, shift>); \
  *(void**)&rt_xchg_not_in_heap = reinterpret_cast<void*>(rtgc_xchg_not_in_heap<T, false, shift>); \
  *(void**)&rt_cmpxchg_not_in_heap = reinterpret_cast<void*>(rtgc_cmpxchg_not_in_heap<T, false, shift>); \
  *(void**)&rt_load_not_in_heap = reinterpret_cast<void*>(rtgc_load_not_in_heap<T, false, shift>); \
  *(void**)&rt_arraycopy_checkcast = reinterpret_cast<void*>(rtgc_arraycopy<T, ARRAYCOPY_CHECKCAST, shift>); \
  *(void**)&rt_arraycopy_disjoint = reinterpret_cast<void*>(rtgc_arraycopy<T, ARRAYCOPY_DISJOINT, shift>); \
  *(void**)&rt_arraycopy_uninitialized = reinterpret_cast<void*>(rtgc_arraycopy<T, IS_DEST_UNINITIALIZED, shift>); \
  *(void**)&rt_arraycopy_conjoint = reinterpret_cast<void*>(rtgc_arraycopy<T, 0, shift>); \

void RtgcBarrier::init_barrier_runtime() {
  if (rt_store != 0) return;
  RTGC::initialize();
  int shift = rtgc_getOopShift();
  switch(shift) {
    case 0:
      INIT_BARRIER_METHODS(narrowOop, 0);
      break;
    case 3:
      INIT_BARRIER_METHODS(narrowOop, 3);
      break;
    case 8:
      INIT_BARRIER_METHODS(oop, 0);
      break;
  }
}

#if 0
template <class ITEM_T, DecoratorSet ds, int shift>
static int rtgc_arraycopy_conjoint(ITEM_T* src_p, ITEM_T* dst_p, 
                    size_t length, arrayOopDesc* dst_array) {
  rtgc_log(LOG_OPT(5), "arraycopy_conjoint (%p)->%p(%p): %d)\n", src_p, dst_array, dst_p, (int)length);
  RTGC::lock_heap();
  ptrdiff_t diff = src_p - dst_p;
  if (diff > 0) {
    ITEM_T* src_end = src_p + length;
    int cp_len = MIN(diff, length);
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* src_item = CompressedOops::decode(*(--src_end));
      if (item != NULL) RTGC::add_referrer(src_item, dst_array);
    }
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* erased = CompressedOops::decode(dst_p[i]);
      if (erased != NULL) RTGC::remove_referrer(erased, dst_array);
    }
  } else {
    int cp_len = MIN(-diff, length);
    ITEM_T* dst_end = dst_p + length;    
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* src_item = CompressedOops::decode(src_p[i]);
      if (item != NULL) RTGC::add_referrer(src_item, dst_array);
    }
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* erased = CompressedOops::decode(*(--dst_end));
      if (erased != NULL) RTGC::remove_referrer(erased, dst_array);
    }
  }
  memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*length);
  RTGC::unlock_heap(true);
  return 0;
}
#endif