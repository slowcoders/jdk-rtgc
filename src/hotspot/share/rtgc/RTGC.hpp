#ifndef __RTGC_HPP__
#define __RTGC_HPP__

#include "oops/accessDecorators.hpp"
#include "oops/compressedOops.hpp"
// #include "oops/oop.inline.hpp"
#include "runtime/atomic.hpp"

extern volatile int ENABLE_RTGC_STORE_HOOK;
extern volatile int ENABLE_RTGC_STORE_TEST;

#define ENABLE_RTGC_LOG  false
#if ENABLE_RTGC_LOG
  #define rtgc_log(...) printf(__VA_ARGS__)
#else
  #define rtgc_log(...) /*ignore*/
#endif

#if 1
  #define RTGC_TRACE()  printf("RTGC: %s:%d %s\n", __FILE__, __LINE__, __FUNCTION__)
#else
  #define RTGC_TRACE() // ignore
#endif
#define RTGC_ASSERT(c, msg)  assert((c) || !ENABLE_RTGC_STORE_HOOK, msg)

inline bool IS_RTGC_ACCESS(DecoratorSet decorators) {
  return (decorators & IN_HEAP) 
      && (decorators & INTERNAL_VALUE_IS_OOP) 
      && ENABLE_RTGC_STORE_HOOK;
}

struct RTGC {

  static volatile int g_mv_lock;

  static bool isPublished(oopDesc* obj);

  static bool lock_heap(oopDesc* obj);

  static void unlock_heap(bool locked);

  static void add_referrer(oopDesc* obj, oopDesc* referrer);

  static void remove_referrer(oopDesc* obj, oopDesc* referrer);

  static oopDesc* RTGC_StoreObjField(oopDesc* obj, ptrdiff_t offset, oopDesc* value, int from);
  static oopDesc* RTGC_CmpXchgObjField(oopDesc* obj, ptrdiff_t offset, oopDesc* cmp_value, oopDesc* new_value);

  static void oop_arraycopy_nocheck(arrayOop dst_array, oop* dst_p, oop* src_p, size_t length);
  static void oop_arraycopy_nocheck(arrayOop dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length);
  static void oop_arraycopy_nocheck(arrayOop dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length);

  static bool oop_arraycopy_checkcast(arrayOop dst_array, oop* dst_p, oop* src_p, size_t length);
  static bool oop_arraycopy_checkcast(arrayOop dst_array, narrowOop* dst_p, narrowOop* src_p, size_t length);
  static bool oop_arraycopy_checkcast(arrayOop dst_array, HeapWord* dst_p, HeapWord* src_p, size_t length);

  static void oop_store(oop obj, ptrdiff_t offset, narrowOop value);
  static void oop_store(oop obj, ptrdiff_t offset, oop value);

  static oop oop_xchg(oop base, ptrdiff_t offset, oop new_value);
  static narrowOop oop_xchg(oop base, ptrdiff_t offset, narrowOop new_value);

  static oop oop_cmpxchg(oop base, ptrdiff_t offset, oop compare_value, oop new_value);
  static narrowOop oop_cmpxchg(oop base, ptrdiff_t offset, narrowOop compare_value, narrowOop new_value);

  static void RTGC_StoreObjField_0(oopDesc* obj, ptrdiff_t offset, oopDesc* value);
  static void RTGC_StoreObjField_3(oopDesc* obj, ptrdiff_t offset, oopDesc* value);
  static void RTGC_StoreObjField_64(oopDesc* obj, ptrdiff_t offset, oopDesc* value);

  static void RTGC_StoreObjArrayItem(arrayOopDesc* array, int index, oopDesc* value, int from);
  static void RTGC_StoreObjArrayItem_0(arrayOopDesc* array, int index, oopDesc* value);
  static void RTGC_StoreObjArrayItem_3(arrayOopDesc* array, int index, oopDesc* value);
  static void RTGC_StoreObjArrayItem_64(arrayOopDesc* array, int index, oopDesc* value);

  static void RTGC_ObjArrayCopy(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length, int callFrom);
  static void RTGC_ObjArrayCopy_0(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_3(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_64(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_0_check(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_3_check(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_64_check(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_0_range(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_3_range(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_64_range(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_0_range_check(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_3_range_check(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
  static void RTGC_ObjArrayCopy_64_range_check(arrayOopDesc* src, int src_pos, arrayOopDesc* dst, int dst_pos, int length);
};

class RTGC_CloneClosure : public BasicOopIterateClosure {
  oopDesc* src;

public:
  RTGC_CloneClosure(oopDesc* src) {
    this->src = src;
  }
  virtual void do_oop(narrowOop* p);
  virtual void do_oop(      oop* p);
};


#endif // __RTGC_HPP__
