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
#include "gc/rtgc/impl/GCObject.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "runtime/atomic.hpp"


volatile int LOG_VERBOSE = 0;
static void* _base = 0;
static int _shift = 0;
static const int MAX_OBJ_SIZE = 256*1024*1024;
static const bool SKIP_UNTRACKABLE = false;
static const bool SEQ_LOCK = true;
namespace RTGC {
  extern bool REF_LINK_ENABLED;
}
static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_BARRIER, function);
}

static bool is_strong_ref(volatile void* addr, oopDesc* base) {
  ptrdiff_t offset = (address)addr - (address)base;
  DecoratorSet ds = AccessBarrierSupport::
      resolve_possibly_unknown_oop_ref_strength<ON_UNKNOWN_OOP_REF>(base, offset);
  if (RtNoDiscoverPhantom || !RtLazyClearWeakHandle) {
    return (ds & ON_PHANTOM_OOP_REF) == 0;
  }
  return true;
}

static void check_field_addr(oopDesc* base, volatile void* addr, bool strong_reachable=true) {
  precond(!strong_reachable || is_strong_ref(addr, base));
  assert(addr > (address)base + oopDesc::klass_offset_in_bytes()
      && addr < (address)base + MAX_OBJ_SIZE
      , "invalid field addr %p of base %p\n", addr, base);
}


static bool is_valid_decorators(DecoratorSet decorators) {
  const DecoratorSet java_refs = ON_PHANTOM_OOP_REF | ON_WEAK_OOP_REF;
  if (decorators & java_refs) {
    return (decorators & AS_NO_KEEPALIVE) != 0;
  }
  return (decorators & AS_NORMAL) != 0;
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

static void raw_set_volatile_field(volatile narrowOop* addr, oopDesc* value) {
  Atomic::release_store(addr, CompressedOops::encode(value));
}
static void raw_set_volatile_field(volatile oop* addr, oopDesc* value) {
  Atomic::release_store((volatile oopDesc**)addr, value);
}

static void raw_set_field(narrowOop* addr, oopDesc* value) {
  RawAccess<>::oop_store(addr, value);
}
static void raw_set_field(oop* addr, oopDesc* value) {
  RawAccess<>::oop_store(addr, value);
}

static oopDesc* raw_atomic_xchg(volatile narrowOop* addr, oopDesc* value) {
  narrowOop n_v = CompressedOops::encode(value);
  narrowOop res = Atomic::xchg(addr, n_v);
  return CompressedOops::decode(res);
}
static oopDesc* raw_atomic_xchg(volatile oop* addr, oopDesc* value) {
  oop n_v = value;
  oop res = Atomic::xchg(addr, n_v);
  return res;
}

static oopDesc* raw_atomic_cmpxchg(volatile narrowOop* addr, oopDesc* compare, oopDesc* value) {
  narrowOop c_v = CompressedOops::encode(compare);
  narrowOop n_v = CompressedOops::encode(value);
  narrowOop res = Atomic::cmpxchg(addr, c_v, n_v);
  return CompressedOops::decode(res);
}
static oopDesc* raw_atomic_cmpxchg(volatile oop* addr, oopDesc* compare, oopDesc* value) {
  oop c_v = compare;
  oop n_v = value;  
  oop res = Atomic::cmpxchg(addr, c_v, n_v);
  return res;
}

void rtgc_update_inverse_graph(oopDesc* base, oopDesc* old_v, oopDesc* new_v, bool do_lock = !SEQ_LOCK) {
#ifdef ASSERT  
  if (!RTGC::REF_LINK_ENABLED) return;
#endif
  if (old_v == new_v) return;

  if (do_lock) RTGC::publish_and_lock_heap(new_v, base);
  if (new_v != NULL && new_v != base) {
    RTGC::add_referrer_ex(new_v, base, true);
  }
  if (old_v != NULL && old_v != base) {
    RTGC::GCObject* oldValue = RTGC::to_obj(old_v);
    RTGC::GCObject* referrer = RTGC::to_obj(base);
    if (!do_lock) {
      RTGC::GCRuntime::disconnectReferenceLink(oldValue, referrer);
    } else {
      int cntSpin = 0;
      while (true) {
        if (RTGC::GCRuntime::tryDisconnectReferenceLink(oldValue, referrer)) {
          break;
        }
        RTGC::unlock_heap(true);
        ::usleep(10);
        rtgc_log(++cntSpin % 10000 == 0, "spin in remove referrer %d\n", cntSpin);
        RTGC::lock_heap();
      }
    }
  }
  if (do_lock) RTGC::unlock_heap(true);
}

void rtgc_update_inverse_graph_c1(oopDesc* base, oopDesc* old_v, oopDesc* new_v) {
  rtgc_log(LOG_OPT(9), "rtgc_update_inverse_graph_c1 %p.%p -> %p\n", base, old_v, new_v);
  if (RTGC::to_obj(base)->isTrackable()) {
    rtgc_update_inverse_graph(base, old_v, new_v, true);
  }
}

template<class T, bool inHeap, int shift>
void rtgc_store(T* addr, oopDesc* new_v, oopDesc* base) {
  if (SKIP_UNTRACKABLE && !((RTGC::GCNode*)RTGC::to_obj(base))->isTrackable()) {
    raw_set_field(addr, new_v);
    rtgc_log(LOG_OPT(11), "skip rtgc_store %p[] <= %p\n", base, new_v);
    return;
  }
  precond(rtHeap::is_trackable(base));
  check_field_addr(base, addr);
  if (SEQ_LOCK) RTGC::lock_heap();
  oopDesc* old = raw_atomic_xchg(addr, new_v);
  rtgc_update_inverse_graph(base, old, new_v);
  if (SEQ_LOCK) RTGC::unlock_heap(true);
}

template<class T, bool dest_uninitialized, int shift>
void rtgc_store_not_in_heap(T* addr, oopDesc* new_v) {
  RTGC::publish_and_lock_heap(new_v);
  oopDesc* old;
  if (dest_uninitialized) {
    old = NULL;
    raw_set_volatile_field(addr, new_v);
  }
  else {
    old = raw_atomic_xchg(addr, new_v);
  }
  //oopDesc* old = dest_uninitialized ? NULL : (oopDesc*)(oop)RawAccess<>::oop_load(addr);
  //raw_set_volatile_field(addr, new_v);
  assert(!(!dest_uninitialized && new_v != NULL && 
      RTGC::debugOptions[2] >= 16 && new_v->klass() == vmClasses::String_klass()), 
        "rtgc_store_not_in_heap %p\n", (void*)new_v);
  RTGC::on_root_changed(old, new_v, addr, "stor");
  RTGC::unlock_heap(true);
}

void (*RtgcBarrier::rt_store)(void* addr, oopDesc* new_v, oopDesc* base) = 0;
void RtgcBarrier::oop_store(oop* addr, oopDesc* new_v, oopDesc* base) {
  rtgc_store<oop, true, 0>(addr, new_v, base);
}

void (*RtgcBarrier::rt_store_not_in_heap)(void* addr, oopDesc* new_v);
void RtgcBarrier::oop_store_not_in_heap(oop* addr, oopDesc* new_v) {
  rtgc_store_not_in_heap<oop, false, 0>(addr, new_v);
}

void (*RtgcBarrier::rt_store_not_in_heap_uninitialized)(void* addr, oopDesc* new_v);
void RtgcBarrier::oop_store_not_in_heap_uninitialized(oop* addr, oopDesc* new_v) {
  rtgc_store_not_in_heap<oop, true, 0>(addr, new_v);
}

void RtgcBarrier::oop_store_unknown(void* addr, oopDesc* new_v, oopDesc* base) {
  precond(rtHeap::is_trackable(base));
  if (is_strong_ref(addr, base)) {
    rt_store(addr, new_v, base);
  }
  else if (UseCompressedOops) {
    raw_set_volatile_field((volatile narrowOop*)addr, new_v);
  } else {
    raw_set_volatile_field((volatile oop*)addr, new_v);
  }
}

template<DecoratorSet decorators, typename T> 
void RtgcBarrier::rt_store_c1(T* addr, oopDesc* new_v, oopDesc* base) {
  rtgc_trace(true, "rt_store_c1 base: %p new_v: %p\n", base, new_v); 

  // precond(rtHeap::is_trackable(base));
  // fatal("gotcha!!");
  if (rtHeap::is_trackable(base)) {
    if (decorators & ON_UNKNOWN_OOP_REF) {
      oop_store_unknown(addr, new_v, base);
    } else {
      rt_store(addr, new_v, base);
    }
  }
  else {
    bool is_volatile = (((decorators & MO_SEQ_CST) != 0) || AlwaysAtomicAccesses);
    if (is_volatile) {
      raw_set_volatile_field(addr, new_v);
    } else {
      raw_set_field(addr, new_v);
    }
  }
}


address RtgcBarrier::getStoreFunction(DecoratorSet decorators) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool initialized = (decorators & IS_DEST_UNINITIALIZED) == 0;
  precond(is_valid_decorators(decorators));
  precond(!in_heap || initialized);

  if (!in_heap) {
    return initialized ? reinterpret_cast<address>(rt_store_not_in_heap)
        : reinterpret_cast<address>(rt_store_not_in_heap_uninitialized);
  }

  bool is_unknown = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  bool is_trackable = (decorators & AS_RAW) == 0;

  if (is_trackable) {
    return !is_unknown ? reinterpret_cast<address>(rt_store)
        : reinterpret_cast<address>(oop_store_unknown);
  }

  bool is_volatile = (((decorators & MO_SEQ_CST) != 0) || AlwaysAtomicAccesses);
  if (RTGC::is_narrow_oop_mode) {
    if (is_unknown) {
      const DecoratorSet ds = ON_UNKNOWN_OOP_REF;
      return !is_volatile ? reinterpret_cast<address>(rt_store_c1<ds, narrowOop>)
          : reinterpret_cast<address>(rt_store_c1<ds|MO_SEQ_CST, narrowOop>);
    } else {
      return !is_volatile ? reinterpret_cast<address>(rt_store_c1<0, narrowOop>)
          : reinterpret_cast<address>(rt_store_c1<0|MO_SEQ_CST, narrowOop>);
    }
  } else {
    if (is_unknown) {
      const DecoratorSet ds = ON_UNKNOWN_OOP_REF;
      return !is_volatile ? reinterpret_cast<address>(rt_store_c1<ds, oop>)
          : reinterpret_cast<address>(rt_store_c1<ds|MO_SEQ_CST, oop>);
    } else {
      return !is_volatile ? reinterpret_cast<address>(rt_store_c1<0, oop>)
          : reinterpret_cast<address>(rt_store_c1<0|MO_SEQ_CST, oop>);
    }
  }
}


template<class T, bool inHeap, int shift>
oopDesc* rtgc_xchg(volatile T* addr, oopDesc* new_v, oopDesc* base) {
  if (SKIP_UNTRACKABLE && !((RTGC::GCNode*)RTGC::to_obj(base))->isTrackable()) {
    oopDesc* res = raw_atomic_xchg(addr, new_v);
    rtgc_log(LOG_OPT(11), "skip rtgc_xchg %p[] <= %p\n", base, new_v);
    return res;
  }
  rtgc_log(LOG_OPT(11), "xchg %p(%p) <-> %p\n", base, addr, new_v);
  check_field_addr(base, addr);
  if (SEQ_LOCK) RTGC::lock_heap();
  oopDesc* old = raw_atomic_xchg(addr, new_v);
  rtgc_update_inverse_graph(base, old, new_v);
  if (SEQ_LOCK) RTGC::unlock_heap(true);
  return old;
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_xchg_not_in_heap(volatile T* addr, oopDesc* new_v) {
  rtgc_log(LOG_OPT(11), "xchg_nh (%p) <-> %p\n", addr, new_v);
  RTGC::publish_and_lock_heap(new_v);
  oopDesc* old = raw_atomic_xchg(addr, new_v);
  // oop old = RawAccess<>::oop_load(addr);
  // raw_set_volatile_field(addr, new_v);
  RTGC::on_root_changed(old, new_v, addr, "xchg");
  RTGC::unlock_heap(true);
  return old;
}

oopDesc* (*RtgcBarrier::rt_xchg)(volatile void* addr, oopDesc* new_v, oopDesc* base);
oopDesc* RtgcBarrier::oop_xchg(volatile oop* addr, oopDesc* new_v, oopDesc* base) {
  return rtgc_xchg<oop, true, 0>(addr, new_v, base);
}

oopDesc* (*RtgcBarrier::rt_xchg_not_in_heap)(volatile void* addr, oopDesc* new_v);
oopDesc* RtgcBarrier::oop_xchg_not_in_heap(volatile oop* addr, oopDesc* new_v) {
  return rtgc_xchg_not_in_heap<oop, false, 0>(addr, new_v);
}

oopDesc* RtgcBarrier::oop_xchg_unknown(volatile void* addr, oopDesc* new_v, oopDesc* base) {
  precond(rtHeap::is_trackable(base));

  rtgc_log(LOG_OPT(11), "rt_xchg_c1 base: %p(%s) rt=%d, yg_root=%d new_v: %p(%s)\n", 
      base, base->klass()->name()->bytes(), is_strong_ref(addr, base), RTGC::to_node(base)->isYoungRoot(), new_v, 
      new_v ? new_v->klass()->name()->bytes() : (const u1*)""); 
  if (is_strong_ref(addr, base)) {
    return rt_xchg(addr, new_v, base);
  }
  if (UseCompressedOops) {
    return raw_atomic_xchg((narrowOop*)addr, new_v);
  } else {
    return raw_atomic_xchg((oop*)addr, new_v);
  }
}

template<DecoratorSet decorators, typename T> 
oopDesc* RtgcBarrier::rt_xchg_c1(T* addr, oopDesc* new_v, oopDesc* base) {
  if (!rtHeap::is_trackable(base)) {
    return raw_atomic_xchg(addr, new_v);
  }
  if (decorators & ON_UNKNOWN_OOP_REF) {
    return oop_xchg_unknown(addr, new_v, base);
  } else {
    return rt_xchg(addr, new_v, base);
  }
}

address RtgcBarrier::getXchgFunction(DecoratorSet decorators) {
  precond(is_valid_decorators(decorators));
  bool in_heap = (decorators & IN_HEAP) != 0;
  if (!in_heap) {
    return reinterpret_cast<address>(rt_xchg_not_in_heap);
  }

  bool is_trackable = (decorators & AS_RAW) == 0;
  bool is_unknown = in_heap && (decorators & ON_UNKNOWN_OOP_REF) != 0;

  if (is_trackable) {
    return !is_unknown ? reinterpret_cast<address>(rt_xchg)
        : reinterpret_cast<address>(oop_xchg_unknown);
  }

  if (RTGC::is_narrow_oop_mode) {
    return !is_unknown ? reinterpret_cast<address>(rt_xchg_c1<0, narrowOop>)
        : reinterpret_cast<address>(rt_xchg_c1<ON_UNKNOWN_OOP_REF, narrowOop>);
  } else {
    return !is_unknown ? reinterpret_cast<address>(rt_xchg_c1<0, oop>)
        : reinterpret_cast<address>(rt_xchg_c1<ON_UNKNOWN_OOP_REF, oop>);
  }
}


template<class T, bool inHeap, int shift>
oopDesc* rtgc_cmpxchg(volatile T* addr, oopDesc* cmp_v, oopDesc* new_v, oopDesc* base) {
  if (SKIP_UNTRACKABLE && !((RTGC::GCNode*)RTGC::to_obj(base))->isTrackable()) {
    oopDesc* res = raw_atomic_cmpxchg(addr, cmp_v, new_v);
    rtgc_log(LOG_OPT(11), "skip rtgc_xchg %p[] <= %p\n", base, new_v);
    return res;
  }
  check_field_addr(base, addr);
  if (SEQ_LOCK) RTGC::lock_heap();
  oopDesc* old = raw_atomic_cmpxchg(addr, cmp_v, new_v);
  if (old == cmp_v) {
    rtgc_update_inverse_graph(base, old, new_v);
  }
  if (SEQ_LOCK) RTGC::unlock_heap(true);
  return old;
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_cmpxchg_not_in_heap(volatile T* addr, oopDesc* cmp_v, oopDesc* new_v) {
  rtgc_log(LOG_OPT(11), "cmpxchg *(%p)=%p <-> %p\n", 
        addr, cmp_v, new_v);
  RTGC::publish_and_lock_heap(new_v);
  oopDesc* old_value = raw_atomic_cmpxchg(addr, cmp_v, new_v);
  // oop old = RawAccess<>::oop_load(addr);
  if (old_value == cmp_v) {
    // raw_set_volatile_field(addr, new_v);
    RTGC::on_root_changed(old_value, new_v, addr, "cmpx");
  }
  RTGC::unlock_heap(true);
  return old_value;
}

oopDesc* (*RtgcBarrier::rt_cmpxchg)(volatile void* addr, oopDesc* cmp_v, oopDesc* new_v, oopDesc* base);
oopDesc* RtgcBarrier::oop_cmpxchg(volatile oop* addr, oopDesc* cmp_v, oopDesc* new_v, oopDesc* base) {
  return rtgc_cmpxchg<oop, true, 0>(addr, cmp_v, new_v, base);
}

oopDesc* (*RtgcBarrier::rt_cmpxchg_not_in_heap)(volatile void* addr, oopDesc* cmp_v, oopDesc* new_v);
oopDesc* RtgcBarrier::oop_cmpxchg_not_in_heap(volatile oop* addr, oopDesc* cmp_v, oopDesc* new_v) {
  return rtgc_cmpxchg_not_in_heap<oop, false, 0>(addr, cmp_v, new_v);
}

oopDesc* RtgcBarrier::oop_cmpxchg_unknown(volatile void* addr, oopDesc* cmp_v, oopDesc* new_v, oopDesc* base) {
  precond(rtHeap::is_trackable(base));
  if (is_strong_ref(addr, base)) {
    return rt_cmpxchg(addr, cmp_v, new_v, base);
  }
  if (UseCompressedOops) {
    return raw_atomic_cmpxchg((narrowOop*)addr, cmp_v, new_v);
  } else {
    return raw_atomic_cmpxchg((oop*)addr, cmp_v, new_v);
  }
}

bool RtgcBarrier::rt_cmpset(volatile void* addr, oopDesc* cmp_v, oopDesc* new_v, oopDesc* base) {
  rtgc_log(LOG_OPT(9), "rt_cmpset %p(%p).%p -> %p\n", base, addr, cmp_v, new_v);
  return cmp_v == rt_cmpxchg(addr, cmp_v, new_v, base);
}
bool RtgcBarrier::rt_cmpset_not_in_heap(volatile void* addr, oopDesc* cmp_v, oopDesc* new_v) {
  rtgc_log(LOG_OPT(9), "rt_cmpset_not_in_heap %p.%p -> %p\n", addr, cmp_v, new_v);
  return cmp_v == rt_cmpxchg_not_in_heap(addr, cmp_v, new_v);
}
bool RtgcBarrier::rt_cmpset_unknown(volatile void* addr, oopDesc* cmp_v, oopDesc* new_v, oopDesc* base) {
  rtgc_log(LOG_OPT(9), "rt_cmpset_unknown %p(%p).%p -> %p\n", base, addr, cmp_v, new_v);
  return cmp_v == oop_cmpxchg_unknown(addr, cmp_v, new_v, base);
}

template<DecoratorSet decorators, typename T> 
oopDesc* RtgcBarrier::rt_cmpset_c1(T* addr, oopDesc* cmp_v, oopDesc* new_v, oopDesc* base) {
  // T* addr = (T*)((address)base + offset);
  rtgc_log(LOG_OPT(9), "rt_cmpset_c1 %p[%p].%p -> %p\n", base, addr, cmp_v, new_v);
  if (!rtHeap::is_trackable(base)) {
    return raw_atomic_cmpxchg(addr, cmp_v, new_v);
  }
  if (decorators & ON_UNKNOWN_OOP_REF) {
    return oop_cmpxchg_unknown(addr, cmp_v, new_v, base);
  } else {
    return rt_cmpxchg(addr, cmp_v, new_v, base);
  }
}


address RtgcBarrier::getCmpSetFunction(DecoratorSet decorators) {
  precond(is_valid_decorators(decorators));
  bool in_heap = (decorators & IN_HEAP) != 0;
  if (!in_heap) {
    return reinterpret_cast<address>(rt_cmpxchg_not_in_heap);
  }

  bool is_trackable = (decorators & AS_RAW) == 0;
  bool is_unknown = (decorators & ON_UNKNOWN_OOP_REF) != 0;

  if (is_trackable) {
    return !is_unknown ? reinterpret_cast<address>(rt_cmpxchg)
        : reinterpret_cast<address>(oop_cmpxchg_unknown);
  }

  if (RTGC::is_narrow_oop_mode) {
    return !is_unknown ? reinterpret_cast<address>(rt_cmpset_c1<0, narrowOop>)
        : reinterpret_cast<address>(rt_cmpset_c1<ON_UNKNOWN_OOP_REF, narrowOop>);
  } else {
    return !is_unknown ? reinterpret_cast<address>(rt_cmpset_c1<0, oop>)
        : reinterpret_cast<address>(rt_cmpset_c1<ON_UNKNOWN_OOP_REF, oop>);
  }
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_load(volatile T* addr, oopDesc* base) {
  // oopDesc->print_on() 함수에서 임의의 필드 값을 access 한다.
  // 오류 발생 시 해당 함수를 수정할 것.
  // check_field_addr(base, addr, true);
  //rtgc_log(LOG_OPT(4), "load %p(%p)\n", base, addr);
  // bool locked = RTGC::lock_heap();
  oop value = RawAccess<>::oop_load(addr);
  // raw_set_volatile_field(addr, new_v);
  // RTGC::unlock_heap(true);
  return value;
}

template<class T, bool inHeap, int shift>
oopDesc* rtgc_load_not_in_heap(volatile T* addr) {
  //rtgc_log(LOG_OPT(4), "load__ (%p)\n", addr);
  // bool locked = RTGC::lock_heap();
  oop value = RawAccess<>::oop_load(addr);
  // raw_set_volatile_field(addr, new_v);
  // RTGC::unlock_heap(true);
  return value;
}

oopDesc* (*RtgcBarrier::rt_load)(volatile void* addr, oopDesc* base) = 0;
oopDesc* RtgcBarrier::oop_load(volatile oop* addr, oopDesc* base) {
  return rtgc_load<oop, true, 0>(addr, base);
}
oopDesc* (*RtgcBarrier::rt_load_not_in_heap)(volatile void* addr);
oopDesc* RtgcBarrier::oop_load_not_in_heap(volatile oop* addr) {
  return rtgc_load_not_in_heap<oop, false, 0>(addr);
}

oopDesc* RtgcBarrier::oop_load_unknown(volatile void* addr, oopDesc* base) {
  precond(rtHeap::is_trackable(base));

  if (is_strong_ref(addr, base)) {
    return rt_load(addr, base);
  }
  oop v;
  if (UseCompressedOops) {
    v = RawAccess<>::oop_load((narrowOop*)addr);
  } else {
    v = RawAccess<>::oop_load((oop*)addr);
  }
  return v;
}

template<DecoratorSet decorators, typename T> 
oopDesc* RtgcBarrier::rt_load_c1(T* addr, oopDesc* base) {
  if (!rtHeap::is_trackable(base)) {
    oop obj = RawAccess<>::oop_load(addr);
    return obj;
  }
  if (decorators & ON_UNKNOWN_OOP_REF) {
    return oop_load_unknown(addr, base);
  } else {
    return rt_load(addr, base);
  }
}

address RtgcBarrier::getLoadFunction(DecoratorSet decorators) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  if (!in_heap) {
    return reinterpret_cast<address>(rt_load_not_in_heap);
  }

  bool is_trackable = (decorators & AS_RAW) == 0;
  bool is_unknown = in_heap && (decorators & ON_UNKNOWN_OOP_REF) != 0;

  if (is_trackable) {
    return !is_unknown ? reinterpret_cast<address>(rt_load)
        : reinterpret_cast<address>(oop_load_unknown);
  }

  if (RTGC::is_narrow_oop_mode) {
    return !is_unknown ? reinterpret_cast<address>(rt_load_c1<0, narrowOop>)
        : reinterpret_cast<address>(rt_load_c1<ON_UNKNOWN_OOP_REF, narrowOop>);
  } else {
    return !is_unknown ? reinterpret_cast<address>(rt_load_c1<0, oop>)
        : reinterpret_cast<address>(rt_load_c1<ON_UNKNOWN_OOP_REF, oop>);
  }

}

//ObjArrayKlass::do_copy -> AccessBarrier::arraycopy_in_heap -> rtgc_arraycopy
template <class ITEM_T, DecoratorSet ds, int shift>
static int rtgc_arraycopy(ITEM_T* src_p, ITEM_T* dst_p, 
                    size_t length, arrayOopDesc* dst_array) {
  bool checkcast = ARRAYCOPY_CHECKCAST & ds;
  bool dest_uninitialized = IS_DEST_UNINITIALIZED & ds;
  rtgc_log(LOG_OPT(5), "arraycopy (%p)->%p(%p): %d) checkcast=%d, uninitialized=%d\n", 
      src_p, dst_array, dst_p, (int)length,
      (ds & ARRAYCOPY_CHECKCAST) != 0, (IS_DEST_UNINITIALIZED & ds) != 0);
  Klass* bound = !checkcast ? NULL
                            : ObjArrayKlass::cast(dst_array->klass())->element_klass();
  RTGC::lock_heap();                          
  for (size_t i = 0; i < length; i++) {
    ITEM_T s_raw = src_p[i]; 
    oopDesc* new_v = CompressedOops::decode(s_raw);
    if (checkcast && new_v != NULL) {
      Klass* stype = new_v->klass();
      if (stype != bound && !stype->is_subtype_of(bound)) {
        memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*i);
        RTGC::unlock_heap(true);
        rtgc_log(LOG_OPT(5), "arraycopy fail (%p)->%p(%p): %d)\n", src_p, dst_array, dst_p, (int)i);
        return i;
      }
    }
    oopDesc* old = dest_uninitialized ? NULL : CompressedOops::decode(dst_p[i]);
    // 사용불가 memmove 필요
    // dst_p[i] = s_raw;
    if (old != new_v) {
      RTGC::on_field_changed(dst_array, old, new_v, &dst_p[i], "arry");
    }
  } 
  memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*length);
  RTGC::unlock_heap(true);
  rtgc_log(LOG_OPT(5), "arraycopy done (%p)->%p(%p): %d)\n", src_p, dst_array, dst_p, (int)length);
  return length;
}

int (*RtgcBarrier::rt_arraycopy_checkcast)(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);
int RtgcBarrier::oop_arraycopy_checkcast(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  return rtgc_arraycopy<oop, ARRAYCOPY_CHECKCAST, 0>(src_p, dst_p, length, dst_array);
}
int RtgcBarrier::oop_arraycopy_checkcast(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  assert(false, "not implemented");
  return false;
}

void (*RtgcBarrier::rt_arraycopy_disjoint)(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);
void RtgcBarrier::oop_arraycopy_disjoint(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  rtgc_arraycopy<oop, ARRAYCOPY_DISJOINT, 0>(src_p, dst_p, length, dst_array);
}
void RtgcBarrier::oop_arraycopy_disjoint(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  fatal("not implemented");
}

void (*RtgcBarrier::rt_arraycopy_uninitialized)(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);
void RtgcBarrier::oop_arraycopy_uninitialized(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  rtgc_arraycopy<oop, ARRAYCOPY_DISJOINT, 0>(src_p, dst_p, length, dst_array);
}
void RtgcBarrier::oop_arraycopy_uninitialized(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  fatal("not implemented");
}

void (*RtgcBarrier::rt_arraycopy_conjoint)(void* src_p, void* dst_p, size_t length, arrayOopDesc* dst_array);
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
  template <class T>
  void do_work(T* p) {
    oop obj = CompressedOops::decode(*p);
    if (obj != NULL) RTGC::add_referrer_ex(obj, _rookie, true);
  }

public:
  RTGC_CloneClosure(oopDesc* rookie) { this->_rookie = rookie; }

  virtual void do_oop(narrowOop* p) { do_work(p); }
  virtual void do_oop(oop*       p) { do_work(p); }
};

void RtgcBarrier::oop_clone_in_heap(oop src, oop dst, size_t size) {
  precond(sizeof(oopDesc) == align_object_size(sizeof(oopDesc)));
  precond(dst->klass() == src->klass());
  dst->init_mark();
  int copy_offset = oopDesc::has_klass_gap() ? oopDesc::klass_offset_in_bytes() : sizeof(oopDesc);
  int copy_size = size - (copy_offset / HeapWordSize);
  AccessInternal::arraycopy_disjoint_words_atomic(
      (address)(oopDesc*)src + copy_offset,
      (address)(oopDesc*)dst + copy_offset,
      copy_size);

  oopDesc* new_obj = dst;
  assert(!RTGC::to_node(new_obj)->isTrackable(), " wrong obj %p\n", new_obj);
  rtgc_log(RTGC::is_debug_pointer(new_obj), "clone_post_barrier %p\n", new_obj); 
  // ((RTGC::GCNode*)RTGC::to_obj(new_obj))->clearFlags();
  rtgc_log(RTGC::to_obj(new_obj)->getRootRefCount() != 0,
    " clone in jvmti %p(rc=%d)\n", new_obj, RTGC::to_obj(new_obj)->getRootRefCount());
  if (false && RTGC::to_node(new_obj)->isTrackable()) {
    rtgc_log(LOG_OPT(11), "clone_post_barrier %p\n", new_obj); 
    RTGC::lock_heap();
    RTGC_CloneClosure c(new_obj);
    new_obj->oop_iterate(&c);
    RTGC::unlock_heap(true);
  }
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
  bool isTrackableArray = to_obj(dst_arry)->isTrackable();
  ptrdiff_t diff = src_p - dst_p;
  if (diff > 0) {
    ITEM_T* src_end = src_p + length;
    int cp_len = MIN(diff, length);
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* src_item = CompressedOops::decode(*(--src_end));
      if (item != NULL && isTrackableArray) RTGC::add_referrer_ex(src_item, dst_array, true);
    }
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* erased = CompressedOops::decode(dst_p[i]);
      if (erased != NULL && isTrackableArray) RTGC::remove_referrer_unsafe(erased, dst_array, true);
    }
  } else {
    int cp_len = MIN(-diff, length);
    ITEM_T* dst_end = dst_p + length;    
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* src_item = CompressedOops::decode(src_p[i]);
      if (item != NULL && isTrackableArray) RTGC::add_referrer_ex(src_item, dst_array, true);
    }
    for (int i = cp_len; --i >= 0; ) {
      oopDesc* erased = CompressedOops::decode(*(--dst_end));
      if (erased != NULL && isTrackableArray) RTGC::remove_referrer_unsafe(erased, dst_array);
    }
  }
  memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*length);
  RTGC::unlock_heap(true);
  return 0;
}
#endif