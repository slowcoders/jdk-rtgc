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
#include "gc/rtgc/rtgc.inline.hpp"


// bool RTGC::isPublished(oopDesc* obj) {
//   return true;
// }

// bool RTGC::lock_heap(oopDesc* obj) {
//   if (!isPublished(obj)) return false;
//   while (Atomic::xchg(&g_mv_lock, 1) != 0) { /* do spin. */ }
//   return true;
// }

// void RTGC::unlock_heap(bool locked) {
//   if (locked) {
//     Atomic::release_store(&g_mv_lock, 0);
//   }
// }

// void RTGC::add_referrer(oopDesc* obj, oopDesc* referrer) {
//     rtgc_log("add_ref: obj=%p(%s), referrer=%p\n", 
//       obj, obj->klass()->name()->bytes(), referrer); 
// }

// void RTGC::remove_referrer(oopDesc* obj, oopDesc* referrer) {
//     rtgc_log("remove_ref: obj=%p(%s), referrer=%p\n",
//       obj, obj->klass()->name()->bytes(), referrer); 
// }

template<class T>
static T rtgc_xchg(oop obj, ptrdiff_t offset, T value) {
  T* slot = (T*)((uintptr_t)(void*)obj + offset);
  bool locked = RTGC::lock_heap(obj);
  T old = *slot;
  *slot = value;
  RTGC::unlock_heap(locked);
  return old;
}

void RTGC::oop_store(oop obj, ptrdiff_t offset, oop value) {
  rtgc_xchg(obj, offset, value);
}
void RTGC::oop_store(oop obj, ptrdiff_t offset, narrowOop value) {
  rtgc_xchg(obj, offset, value);
}

oop RTGC::oop_xchg(oop base, ptrdiff_t offset, oop new_value) {
  return rtgc_xchg(base, offset, new_value);
}
narrowOop RTGC::oop_xchg(oop base, ptrdiff_t offset, narrowOop new_value) {
  return rtgc_xchg(base, offset, new_value);
}

template<class T>
static T rtgc_cmpxchg(oop base, ptrdiff_t offset, T compare_value, T new_value) {
  T* slot = (T*)((uintptr_t)(void*)base + offset);
  bool locked = RTGC::lock_heap(base);
  T old = *slot;
  if (old == compare_value) {
    *slot = new_value;
  }
  RTGC::unlock_heap(locked);
  return old;

}
oop RTGC::oop_cmpxchg(oop base, ptrdiff_t offset, oop compare_value, oop new_value) {
  return rtgc_cmpxchg(base, offset, compare_value, new_value);
}
narrowOop RTGC::oop_cmpxchg(oop base, ptrdiff_t offset, narrowOop compare_value, narrowOop new_value) {
  return rtgc_cmpxchg(base, offset, compare_value, new_value);
}

template <DecoratorSet ds, class ITEM_T>
static bool rtgc_arraycopy(arrayOop dst, ITEM_T* dst_p, ITEM_T* src_p,
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

void RTGC::oop_arraycopy_nocheck(arrayOop dst_array, oop* dst_p, oop* src_p, size_t length) {
  rtgc_arraycopy<0, oop>(dst_array, dst_p, src_p, length);
}
void RTGC::oop_arraycopy_nocheck(arrayOop dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length) {
  rtgc_arraycopy<0, narrowOop>(dst_array, dst_p, src_p, length);
}
void RTGC::oop_arraycopy_nocheck(arrayOop dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length) {
  assert(false, "not implemented");
  //rtgc_arraycopy<0, HeapWord>(dst_array, dst_p, src_p, length);
}

bool RTGC::oop_arraycopy_checkcast(arrayOop dst_array, oop* dst_p, oop* src_p, size_t length) {
  return rtgc_arraycopy<ARRAYCOPY_CHECKCAST, oop>(dst_array, dst_p, src_p, length);
}
bool RTGC::oop_arraycopy_checkcast(arrayOop dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length) {
  return rtgc_arraycopy<ARRAYCOPY_CHECKCAST, narrowOop>(dst_array, dst_p, src_p, length);
}
bool RTGC::oop_arraycopy_checkcast(arrayOop dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length) {
  assert(false, "not implemented");
  return false;
  //rtgc_arraycopy<ARRAYCOPY_CHECKCAST, narrowOop>(dst_array, dst_p, src_p, length);
}



JRT_LEAF(oopDesc*, RTGC::RTGC_StoreObjField(oopDesc* obj, ptrdiff_t offset, oopDesc* value, int from)) 
  // 참고) Array 참조 시에도 상수 인덱스를 사용하면 본 함수가 호출된다. 
  oop old;
#ifdef _LP64
  if (UseCompressedOops) {
    old = CompressedOops::decode(rtgc_xchg(obj, offset, CompressedOops::encode(value)));
  }
  else {
    old = rtgc_xchg(obj, offset, (oop)value);
  }
#else
  old = rtgc_xchg(obj, offset, (narrowOop)value);
#endif
  return (oopDesc*)old;
JRT_END

JRT_LEAF(oopDesc*, RTGC::RTGC_CmpXchgObjField(oopDesc* obj, ptrdiff_t offset, oopDesc* cmp_value, oopDesc* new_value)) 
  // 주의) Array 참조 시에도 상수 인덱스를 사용하면 본 함수가 호출된다. 
  oop old;
#ifdef _LP64
  if (UseCompressedOops) {
    old = CompressedOops::decode(rtgc_cmpxchg(obj, offset, 
      CompressedOops::encode(cmp_value), CompressedOops::encode(new_value)));
  }
  else {
    old = rtgc_cmpxchg(obj, offset, cmp_value, new_value);
  }
#else
  old = rtgc_cmpxchg(obj, offset, (narrowOop)value);
#endif
  return (oopDesc*)old;
JRT_END


JRT_LEAF(void, RTGC::RTGC_StoreObjArrayItem(arrayOopDesc* array, int index, oopDesc* value, int from)) 
#ifdef _LP64
  int offset;
  if (UseCompressedOops) {
    offset = index * sizeof(narrowOop) + 16;
    rtgc_xchg(array, offset, CompressedOops::encode(value));
  }
  else {
    offset = index * sizeof(oop) + 16/*???*/;
    rtgc_xchg(array, offset, (oop)value);
  }
#else
  offset = index * sizeof(narrowOop) + 16/*???*/;
  rtgc_xchg(array, offset, (narrowOop)value);
#endif
  return;
JRT_END

JRT_LEAF(void, RTGC::RTGC_ObjArrayCopy(arrayOopDesc* s, int src_pos, arrayOopDesc* d, int dst_pos, int length, int flags)) 
  if (ENABLE_RTGC_STORE_TEST) {
    rtgc_log("ARRAY_COPY[%d]: src=%p(%s), src_pos=%d, dst=%p, dst_pos=%d, len=%d\n", 
      flags, s, s->klass()->name()->bytes(),
      src_pos, d, dst_pos, length);
  }

  JavaThread* __the_thread__ = JavaThread::current();
  if (!RTGCArray::check_arraycopy_offsets(s, src_pos, d, dst_pos, length, THREAD)) {
    return;
  }

  if (UseCompressedOops) {
    size_t src_offset = (size_t) objArrayOopDesc::obj_at_offset<narrowOop>(src_pos);
    size_t dst_offset = (size_t) objArrayOopDesc::obj_at_offset<narrowOop>(dst_pos);
    assert(arrayOopDesc::obj_offset_to_raw<narrowOop>(s, src_offset, NULL) ==
           objArrayOop(s)->obj_at_addr_raw<narrowOop>(src_pos), "sanity");
    assert(arrayOopDesc::obj_offset_to_raw<narrowOop>(d, dst_offset, NULL) ==
           objArrayOop(d)->obj_at_addr_raw<narrowOop>(dst_pos), "sanity");
    ObjArrayKlass::do_copy(s, src_offset, d, dst_offset, length, CHECK);
  } else {
    size_t src_offset = (size_t) objArrayOopDesc::obj_at_offset<oop>(src_pos);
    size_t dst_offset = (size_t) objArrayOopDesc::obj_at_offset<oop>(dst_pos);
    assert(arrayOopDesc::obj_offset_to_raw<oop>(s, src_offset, NULL) ==
           objArrayOop(s)->obj_at_addr_raw<oop>(src_pos), "sanity");
    assert(arrayOopDesc::obj_offset_to_raw<oop>(d, dst_offset, NULL) ==
           objArrayOop(d)->obj_at_addr_raw<oop>(dst_pos), "sanity");
    ObjArrayKlass::do_copy(s, src_offset, d, dst_offset, length, CHECK);
  }

JRT_END

void RTGC_oop_arraycopy2() {}


