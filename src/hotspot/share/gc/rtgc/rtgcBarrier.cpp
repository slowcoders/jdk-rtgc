#include "precompiled.hpp"
#include "jvm.h"
#include "aot/aotLoader.hpp"
#include "classfile/stringTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "code/codeCache.hpp"
#include "code/compiledIC.hpp"
#include "code/icBuffer.hpp"
#include "code/compiledMethod.inline.hpp"
#include "code/scopeDesc.hpp"
#include "code/vtableStubs.hpp"
#include "compiler/abstractCompiler.hpp"
#include "compiler/compileBroker.hpp"
#include "compiler/disassembler.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/gcLocker.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "jfr/jfrEvents.hpp"
#include "logging/log.hpp"
#include "memory/metaspaceShared.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "oops/klass.hpp"
#include "oops/method.inline.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/forte.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/methodHandles.hpp"
#include "prims/nativeLookup.hpp"
#include "runtime/arguments.hpp"
#include "runtime/atomic.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/init.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vframe.inline.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/copy.hpp"
#include "utilities/dtrace.hpp"
#include "utilities/events.hpp"
#include "utilities/hashtable.inline.hpp"
#include "utilities/macros.hpp"
#include "utilities/xmlstream.hpp"
#ifdef COMPILER1
#include "c1/c1_Runtime1.hpp"
#endif

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgc_jrt.hpp"

volatile int ENABLE_RTGC_STORE_TEST = 0;
volatile int ENABLE_RTGC_STORE_HOOK = 0;

volatile int RTGC::g_mv_lock = (0);

bool RTGC::isPublished(oopDesc* obj) {
  return true;
}

bool RTGC::lock_heap(oopDesc* obj) {
  if (!isPublished(obj)) return false;
  while (Atomic::xchg(&g_mv_lock, 1) != 0) { /* do spin. */ }
  return true;
}

void RTGC::unlock_heap(bool locked) {
  if (locked) {
    Atomic::release_store(&g_mv_lock, 0);
  }
}

void RTGC::add_referrer(oopDesc* obj, oopDesc* referrer) {
    rtgc_log("add_ref: obj=%p(%s), referrer=%p\n", 
      obj, obj->klass()->name()->bytes(), referrer); 
}

void RTGC::remove_referrer(oopDesc* obj, oopDesc* referrer) {
    rtgc_log("remove_ref: obj=%p(%s), referrer=%p\n",
      obj, obj->klass()->name()->bytes(), referrer); 
}

static void rtgc_store(volatile narrowOop* addr, oopDesc* value) {
  *addr = CompressedOops::encode(value);
}
static void rtgc_store(volatile oop* addr, oopDesc* value) {
  *(oop*)addr = value;
}

template<class T, bool inHeap>
oopDesc* rtgc_xchg(oopDesc* obj, volatile T* addr, oopDesc* new_value) {
  printf("xchg0 *%p(%p) = %p\n", obj, addr, new_value);
  bool locked = RTGC::lock_heap(obj);
  oopDesc* old = CompressedOops::decode(*addr);
  rtgc_store(addr, new_value);
  RTGC::unlock_heap(locked);
  return old;
}

oopDesc* RtgcBarrier::oop_xchg(oopDesc* base, volatile narrowOop* addr, oopDesc* new_value) {
  return rtgc_xchg<narrowOop, true>(base, addr, new_value);
}
oopDesc* RtgcBarrier::oop_xchg(oopDesc* base, volatile oop* addr, oopDesc* new_value) {
  return rtgc_xchg<oop, true>(base, addr, new_value);
}
oopDesc* RtgcBarrier::oop_xchg_in_root(volatile narrowOop* addr, oopDesc* new_value) {
  return rtgc_xchg<narrowOop, false>(NULL, addr, new_value);
}
oopDesc* RtgcBarrier::oop_xchg_in_root(volatile oop* addr, oopDesc* new_value) {
  return rtgc_xchg<oop, false>(NULL, addr, new_value);
}

template<class T, bool inHeap>
oopDesc* rtgc_cmpxchg(oopDesc* base, volatile T* addr, oopDesc* compare_value, oopDesc* new_value) {
  printf("cmpxchg0 *%p(%p) = %p->%p\n", base, addr, compare_value, new_value);
  bool locked = RTGC::lock_heap(base);
  oopDesc* old = CompressedOops::decode(*addr);
  if (old == compare_value) {
    rtgc_store(addr, new_value);
  }
  RTGC::unlock_heap(locked);
  return old;
}

oopDesc* RtgcBarrier::oop_cmpxchg(oopDesc* base, volatile narrowOop* addr, oopDesc* compare_value, oopDesc* new_value) {
  return rtgc_cmpxchg<narrowOop, true>(base, addr, compare_value, new_value);
}
oopDesc* RtgcBarrier::oop_cmpxchg(oopDesc* base, volatile oop* addr, oopDesc* compare_value, oopDesc* new_value) {
  return rtgc_cmpxchg<oop, true>(base, addr, compare_value, new_value);
}
oopDesc* RtgcBarrier::oop_cmpxchg_in_root(volatile narrowOop* addr, oopDesc* compare_value, oopDesc* new_value) {
  return rtgc_cmpxchg<narrowOop, false>(NULL, addr, compare_value, new_value);
}
oopDesc* RtgcBarrier::oop_cmpxchg_in_root(volatile oop* addr, oopDesc* compare_value, oopDesc* new_value) {
  return rtgc_cmpxchg<oop, false>(NULL, addr, compare_value, new_value);
}

//ObjArrayKlass::do_copy -> AccessBarrier::arraycopy_in_heap -> rtgc_arraycopy
template <DecoratorSet ds, class ITEM_T>
static bool rtgc_arraycopy(arrayOopDesc*  dst, ITEM_T* dst_p, ITEM_T* src_p,
                    size_t length) {
  bool checkcast = ARRAYCOPY_CHECKCAST & ds;
  Klass* bound = !checkcast ? NULL
                            : ObjArrayKlass::cast(dst->klass())->element_klass();
  bool locked = RTGC::lock_heap(dst);                          
  for (size_t i = 0; i < length; i++) {
    ITEM_T s_raw = src_p[i]; 
    oopDesc* item = CompressedOops::decode(s_raw);
    if (checkcast && item != NULL) {
      Klass* stype = item->klass();
      if (stype != bound && !stype->is_subtype_of(bound)) {
        memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*i);
        RTGC::unlock_heap(locked);
        return false; 
      }
    }
    oopDesc* old = CompressedOops::decode(dst_p[i]);
    //dst_p[i] = s_raw; // ARRAYCOPY_DISJOINT
    if (item != NULL) RTGC::add_referrer(CompressedOops::decode(item), dst);
    if (old != NULL) RTGC::remove_referrer(CompressedOops::decode(old), dst);
  } 
  memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*length);
  RTGC::unlock_heap(locked);
  return true;
}

void RtgcBarrier::oop_arraycopy_nocheck(arrayOopDesc*  dst_array, oop* dst_p, oop* src_p, size_t length) {
  rtgc_arraycopy<0, oop>(dst_array, dst_p, src_p, length);
}
void RtgcBarrier::oop_arraycopy_nocheck(arrayOopDesc*  dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length) {
  rtgc_arraycopy<0, narrowOop>(dst_array, dst_p, src_p, length);
}
void RtgcBarrier::oop_arraycopy_nocheck(arrayOopDesc*  dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length) {
  assert(false, "not implemented");
  //rtgc_arraycopy<0, HeapWord>(dst_array, dst_p, src_p, length);
}

bool RtgcBarrier::oop_arraycopy_checkcast(arrayOopDesc*  dst_array, oop* dst_p, oop* src_p, size_t length) {
  return rtgc_arraycopy<ARRAYCOPY_CHECKCAST, oop>(dst_array, dst_p, src_p, length);
}
bool RtgcBarrier::oop_arraycopy_checkcast(arrayOopDesc*  dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length) {
  return rtgc_arraycopy<ARRAYCOPY_CHECKCAST, narrowOop>(dst_array, dst_p, src_p, length);
}
bool RtgcBarrier::oop_arraycopy_checkcast(arrayOopDesc*  dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length) {
  assert(false, "not implemented");
  return false;
  //rtgc_arraycopy<ARRAYCOPY_CHECKCAST, narrowOop>(dst_array, dst_p, src_p, length);
}

void RtgcBarrier::clone_post_barrier(oopDesc*  new_array) {
  bool locked = RTGC::lock_heap(new_array);
  RTGC_CloneClosure c(new_array);
  new_array->oop_iterate(&c);
  RTGC::unlock_heap(locked);
}


// JRT_LEAF(oopDesc*, RtgcBarrier::RTGC_StoreObjField(oopDesc* obj, narrowOop* addr, oopDesc* value, int from)) 
//   // 참고) Array 참조 시에도 상수 인덱스를 사용하면 본 함수가 호출된다. 
//   oopDesc* old;
// #ifdef _LP64
//   if (UseCompressedOops) {
//     old = CompressedOops::decode(rtgc_xchg(obj, addr, CompressedOops::encode(value)));
//   }
//   else {
//     old = rtgc_xchg(obj, addr, (oop)value);
//   }
// #else
//   old = rtgc_xchg(obj, addr, (narrowOop)value);
// #endif
//   return (oopDesc*)old;
// JRT_END

// JRT_LEAF(oopDesc*, RtgcBarrier::RTGC_CmpXchgObjField(oopDesc* obj, narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value)) 
//   // 주의) Array 참조 시에도 상수 인덱스를 사용하면 본 함수가 호출된다. 
//   oopDesc* old;
// #ifdef _LP64
//   if (UseCompressedOops) {
//     old = CompressedOops::decode(rtgc_cmpxchg(obj, addr, 
//       CompressedOops::encode(cmp_value), CompressedOops::encode(new_value)));
//   }
//   else {
//     old = rtgc_cmpxchg(obj, addr, cmp_value, new_value);
//   }
// #else
//   old = rtgc_cmpxchg(obj, addr, (narrowOop)value);
// #endif
//   return (oopDesc*)old;
// JRT_END


// JRT_LEAF(void, RtgcBarrier::RTGC_StoreObjArrayItem(arrayOopDesc* array, int index, oopDesc* value, int from)) 
// #ifdef _LP64
//   int addr;
//   if (UseCompressedOops) {
//     addr = index * sizeof(narrowOop) + 16;
//     rtgc_xchg(array, addr, CompressedOops::encode(value));
//   }
//   else {
//     addr = index * sizeof(oop) + 16/*???*/;
//     rtgc_xchg(array, addr, (oop)value);
//   }
// #else
//   addr = index * sizeof(narrowOop) + 16/*???*/;
//   rtgc_xchg(array, addr, (narrowOop)value);
// #endif
//   return;
// JRT_END

// JRT_LEAF(void, RtgcBarrier::RTGC_ObjArrayCopy(arrayOopDesc* s, int src_pos, arrayOopDesc* d, int dst_pos, int length, int flags)) 
//   if (ENABLE_RTGC_STORE_TEST) {
//     rtgc_log("ARRAY_COPY[%d]: src=%p(%s), src_pos=%d, dst=%p, dst_pos=%d, len=%d\n", 
//       flags, s, s->klass()->name()->bytes(),
//       src_pos, d, dst_pos, length);
//   }

//   JavaThread* __the_thread__ = JavaThread::current();
//   if (!RTGCArray::check_arraycopy_offsets(s, src_pos, d, dst_pos, length, THREAD)) {
//     return;
//   }

//   if (UseCompressedOops) {
//     size_t src_offset = (size_t) objArrayOopDesc::obj_at_offset<narrowOop>(src_pos);
//     size_t dst_offset = (size_t) objArrayOopDesc::obj_at_offset<narrowOop>(dst_pos);
//     assert(arrayOopDesc::obj_offset_to_raw<narrowOop>(s, src_offset, NULL) ==
//            objArrayOop(s)->obj_at_addr_raw<narrowOop>(src_pos), "sanity");
//     assert(arrayOopDesc::obj_offset_to_raw<narrowOop>(d, dst_offset, NULL) ==
//            objArrayOop(d)->obj_at_addr_raw<narrowOop>(dst_pos), "sanity");
//     ObjArrayKlass::do_copy(s, src_offset, d, dst_offset, length, CHECK);
//   } else {
//     size_t src_offset = (size_t) objArrayOopDesc::obj_at_offset<oop>(src_pos);
//     size_t dst_offset = (size_t) objArrayOopDesc::obj_at_offset<oop>(dst_pos);
//     assert(arrayOopDesc::obj_offset_to_raw<oop>(s, src_offset, NULL) ==
//            objArrayOop(s)->obj_at_addr_raw<oop>(src_pos), "sanity");
//     assert(arrayOopDesc::obj_offset_to_raw<oop>(d, dst_offset, NULL) ==
//            objArrayOop(d)->obj_at_addr_raw<oop>(dst_pos), "sanity");
//     ObjArrayKlass::do_copy(s, src_offset, d, dst_offset, length, CHECK);
//   }

// JRT_END

// void RTGC_oop_arraycopy2() {}


JRT_LEAF(oopDesc*, rtgc_oop_xchg_0(oopDesc* base, ptrdiff_t offset, oopDesc* new_value))
  narrowOop* addr = (narrowOop*)((address)base + offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_xchg_3(oopDesc* base, ptrdiff_t offset, oopDesc* new_value))
  narrowOop* addr = (narrowOop*)((address)base + offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_xchg_8(oopDesc* base, ptrdiff_t offset, oopDesc* new_value))
  oop* addr = (oop*)((address)base + offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_array_xchg_0(arrayOopDesc* array, size_t index, oopDesc* new_value))
  size_t offset = index * sizeof(narrowOop) + 16;
  narrowOop* addr = (narrowOop*)((address)base + offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_array_xchg_3(arrayOopDesc* array, size_t index, oopDesc* new_value))
  size_t offset = index * sizeof(narrowOop) + 16;
  narrowOop* addr = (narrowOop*)((address)base + offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_array_xchg_8(arrayOopDesc* array, size_t index, oopDesc* new_value))
  size_t offset = index * sizeof(narrowOop) + 16;
  oop* addr = (oop*)((address)base + offset);
  assert(false, "Not Implemented");
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_cmpxchg_0(oopDesc* base, ptrdiff_t offset, oopDesc* cmp_value, oopDesc* new_value))
  narrowOop* addr = (narrowOop*)((address)base + offset);
  return RtgcBarrier::oop_cmpxchg(base, addr, cmp_value, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_cmpxchg_3(oopDesc* base, ptrdiff_t offset, oopDesc* cmp_value, oopDesc* new_value))
  narrowOop* addr = (narrowOop*)((address)base + offset);
  return RtgcBarrier::oop_cmpxchg(base, addr, cmp_value, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_cmpxchg_8(oopDesc* base, ptrdiff_t offset, oopDesc* cmp_value, oopDesc* new_value))
  oop* addr = (oop*)((address)base + offset);
  return RtgcBarrier::oop_cmpxchg(base, addr, cmp_value, new_value);
JRT_END

template <class T> void do_oop_work(T* p, oopDesc* src) {
  T const o = RawAccess<>::oop_load(p);
  oopDesc* v = CompressedOops::decode(o);
  if (v != NULL) {
    RTGC::add_referrer(v, src);
  }
}

void RTGC_CloneClosure::do_oop(narrowOop* p) { do_oop_work(p, src); }
void RTGC_CloneClosure::do_oop(      oop* p) { do_oop_work(p, src); }

