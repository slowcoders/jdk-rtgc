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


static void rtgc_store(volatile narrowOop* addr, oopDesc* value) {
  *addr = CompressedOops::encode(value);
}
static void rtgc_store(volatile oop* addr, oopDesc* value) {
  *(oop*)addr = value;
}

template<class T, bool inHeap>
oopDesc* rtgc_xchg(oopDesc* base, volatile T* addr, oopDesc* new_value) {
  // printf("xchg0 *%p(%p) = %p\n", base, addr, new_value);
  bool locked = RTGC::lock_heap(base);
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
  // printf("cmpxchg0 *%p(%p) = %p->%p\n", base, addr, compare_value, new_value);
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


template<class T, bool inHeap>
oopDesc* rtgc_load(oopDesc* base, volatile T* addr) {
  // printf("load0 *%p(%p)\n", base, addr);
  // bool locked = RTGC::lock_heap(base);
  oopDesc* value = CompressedOops::decode(*addr);
  // rtgc_store(addr, new_value);
  // RTGC::unlock_heap(locked);
  return value;
}

oopDesc* RtgcBarrier::oop_load(oopDesc* base, volatile narrowOop* addr) {
  return rtgc_load<narrowOop, true>(base, addr);
}
oopDesc* RtgcBarrier::oop_load(oopDesc* base, volatile oop* addr) {
  return rtgc_load<oop, true>(base, addr);
}
oopDesc* RtgcBarrier::oop_load_in_root(volatile narrowOop* addr) {
  return rtgc_load<narrowOop, false>(NULL, addr);
}
oopDesc* RtgcBarrier::oop_load_in_root(volatile oop* addr) {
  return rtgc_load<oop, false>(NULL, addr);
}

//ObjArrayKlass::do_copy -> AccessBarrier::arraycopy_in_heap -> rtgc_arraycopy
template <DecoratorSet ds, class ITEM_T>
static int rtgc_arraycopy(arrayOopDesc* dst_array, ITEM_T* dst_p, ITEM_T* src_p,
                    size_t length) {
  bool checkcast = ARRAYCOPY_CHECKCAST & ds;
  Klass* bound = !checkcast ? NULL
                            : ObjArrayKlass::cast(dst_array->klass())->element_klass();
  bool locked = RTGC::lock_heap(dst_array);                          
  for (size_t i = 0; i < length; i++) {
    ITEM_T s_raw = src_p[i]; 
  // for (ITEM_T* end_p = src_p + length; src_p < end_p; src_p ++, dst_p ++) {
  //   ITEM_T s_raw = src_p[0]; 
    oopDesc* item = CompressedOops::decode(s_raw);
    if (checkcast && item != NULL) {
      Klass* stype = item->klass();
      if (stype != bound && !stype->is_subtype_of(bound)) {
        memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*i);
        RTGC::unlock_heap(locked);
        return length - i;//(end_p - src_p); // returns remain_count;
      }
    }
    oopDesc* old = CompressedOops::decode(dst_p[i]);
    // 사용불가 memmove 필요
    // dst_p[i] = s_raw;
    if (item != NULL) RTGC::add_referrer(CompressedOops::decode(item), dst_array);
    if (old != NULL) RTGC::remove_referrer(CompressedOops::decode(old), dst_array);
  } 
  memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*length);
  RTGC::unlock_heap(locked);
  return 0;
}

void RtgcBarrier::oop_arraycopy_nocheck(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  rtgc_arraycopy<0, oop>(dst_array, dst_p, src_p, length);
}
void RtgcBarrier::oop_arraycopy_nocheck(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array) {
  rtgc_arraycopy<0, narrowOop>(dst_array, dst_p, src_p, length);
}
void RtgcBarrier::oop_arraycopy_nocheck(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  assert(false, "not implemented");
  //rtgc_arraycopy<0, HeapWord>(dst_array, dst_p, src_p, length);
}

int RtgcBarrier::oop_arraycopy_checkcast(oop* src_p, oop* dst_p, size_t length, arrayOopDesc* dst_array) {
  return rtgc_arraycopy<ARRAYCOPY_CHECKCAST, oop>(dst_array, dst_p, src_p, length);
}
int RtgcBarrier::oop_arraycopy_checkcast(narrowOop* src_p, narrowOop* dst_p, size_t length, arrayOopDesc* dst_array) {
  return rtgc_arraycopy<ARRAYCOPY_CHECKCAST, narrowOop>(dst_array, dst_p, src_p, length);
}
int RtgcBarrier::oop_arraycopy_checkcast(HeapWord* src_p, HeapWord* dst_p, size_t length, arrayOopDesc* dst_array) {
  assert(false, "not implemented");
  return false;
  //rtgc_arraycopy<ARRAYCOPY_CHECKCAST, HeapWord>(dst_array, dst_p, src_p, length);
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
  bool locked = RTGC::lock_heap(new_array);
  RTGC_CloneClosure c(new_array);
  new_array->oop_iterate(&c);
  RTGC::unlock_heap(locked);
}





JRT_LEAF(oopDesc*, rtgc_oop_xchg_0(oopDesc* base, ptrdiff_t offset, oopDesc* new_value))
  narrowOop* addr = (narrowOop*)((address)base + offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_xchg_3(oopDesc* base, ptrdiff_t offset, oopDesc* new_value))
  narrowOop* addr = (narrowOop*)((address)base + offset);
  printf("xchg3 %p(%p) = %p\n", base, addr, new_value);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_xchg_8(oopDesc* base, ptrdiff_t offset, oopDesc* new_value))
  oop* addr = (oop*)((address)base + offset);
  return RtgcBarrier::oop_xchg(base, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_array_xchg_0(arrayOopDesc* array, size_t index, oopDesc* new_value))
  size_t offset = index * sizeof(narrowOop) + 16;
  narrowOop* addr = (narrowOop*)((address)array + offset);
  return RtgcBarrier::oop_xchg(array, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_array_xchg_3(arrayOopDesc* array, size_t index, oopDesc* new_value))
  size_t offset = index * sizeof(narrowOop) + 16;
  narrowOop* addr = (narrowOop*)((address)array + offset);
  return RtgcBarrier::oop_xchg(array, addr, new_value);
JRT_END

JRT_LEAF(oopDesc*, rtgc_oop_array_xchg_8(arrayOopDesc* array, size_t index, oopDesc* new_value))
  size_t offset = index * sizeof(narrowOop) + 16;
  oop* addr = (oop*)((address)array + offset);
  assert(false, "Not Implemented");
  return RtgcBarrier::oop_xchg(array, addr, new_value);
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


template <DecoratorSet ds, class ITEM_T>
static int rtgc_arraycopy22(arrayOopDesc* dst_array, ITEM_T* dst_p, ITEM_T* src_p,
                    size_t length) {
  bool checkcast = ARRAYCOPY_CHECKCAST & ds;
  Klass* bound = !checkcast ? NULL
                            : ObjArrayKlass::cast(dst_array->klass())->element_klass();
  bool locked = RTGC::lock_heap(dst_array);                          
  for (ITEM_T* end_p = src_p + length; src_p < end_p; src_p ++, dst_p ++) {
    ITEM_T s_raw = src_p[0]; 
    oopDesc* item = CompressedOops::decode(s_raw);
    if (checkcast && item != NULL) {
      Klass* stype = item->klass();
      if (stype != bound && !stype->is_subtype_of(bound)) {
        RTGC::unlock_heap(locked);
        return (end_p - src_p); // returns remain_count;
      }
    }
    oopDesc* old = CompressedOops::decode(dst_p[0]);
    dst_p[0] = s_raw;
    if (item != NULL) RTGC::add_referrer(CompressedOops::decode(item), dst_array);
    if (old != NULL) RTGC::remove_referrer(CompressedOops::decode(old), dst_array);
  } 
  RTGC::unlock_heap(locked);
  return 0;
}
