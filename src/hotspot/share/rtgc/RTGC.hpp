#ifndef __RTGC_HPP__
#define __RTGC_HPP__

#include "oops/accessDecorators.hpp"
#include "oops/compressedOops.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/atomic.hpp"

extern volatile int ENABLE_RTGC_STORE_HOOK;
extern volatile int ENABLE_RTGC_STORE_TEST;

#define ENABLE_RTGC_LOG  false
#if ENABLE_RTGC_LOG
  #define rtgc_log(...) printf(__VA_ARGS__)
#else
  #define rtgc_log(...) /*ignore*/
#endif

struct RTGC {

  static volatile int g_mv_lock;

  static bool isPublished(oopDesc* obj) {
    return true;
  }

  static bool lock_refLink(oopDesc* obj) {
    if (!isPublished(obj)) return false;
    while (Atomic::xchg(&g_mv_lock, 1) != 0) { /* do spin. */ }
    return true;
  }

  static void unlock_refLink(bool locked) {
    if (locked) {
      Atomic::release_store(&g_mv_lock, 0);
    }
  }

  static void add_referrer(oopDesc* obj, oopDesc* referrer) {
      rtgc_log("add_ref: obj=%p(%s), referrer=%p\n", 
        obj, obj->klass()->name()->bytes(), referrer); 
  }

  static void remove_referrer(oopDesc* obj, oopDesc* referrer) {
      rtgc_log("remove_ref: obj=%p(%s), referrer=%p\n",
        obj, obj->klass()->name()->bytes(), referrer); 
  }

  static void RTGC_StoreStaticField(oopDesc* obj, int offset, oopDesc* value, int from);
  static oopDesc* RTGC_StoreObjField(oopDesc* obj, int offset, oopDesc* value, int from);
  static oopDesc* RTGC_CmpXchgObjField(oopDesc* obj, int offset, oopDesc* cmp_value, oopDesc* new_value);
  static void RTGC_StoreObjField_0(oopDesc* obj, int offset, oopDesc* value);
  static void RTGC_StoreObjField_3(oopDesc* obj, int offset, oopDesc* value);
  static void RTGC_StoreObjField_64(oopDesc* obj, int offset, oopDesc* value);

  static void RTGC_StoreObjArrayItem(oopDesc* obj, int index, oopDesc* value, int from);
  static void RTGC_StoreObjArrayItem_0(oopDesc* obj, int index, oopDesc* value);
  static void RTGC_StoreObjArrayItem_3(oopDesc* obj, int index, oopDesc* value);
  static void RTGC_StoreObjArrayItem_64(oopDesc* obj, int index, oopDesc* value);

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


#endif // __RTGC_HPP__
