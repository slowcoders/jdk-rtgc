
#ifndef __RTGC_ARRAY_HPP__
#define __RTGC_ARRAY_HPP__

#include "rtgc/RTGC.hpp"

//extern volatile int ENABLE_RTGC_STORE_HOOK;
void RTGC_oop_arraycopy2();

struct RTGCArray {
  template <DecoratorSet ds, class ITEM_T>
  static bool oop_arraycopy(arrayOop src, ITEM_T* src_p,
                      arrayOop dst, ITEM_T* dst_p,
                      size_t length) {
    bool checkcast = ARRAYCOPY_CHECKCAST & ds;
    Klass* bound = !checkcast ? NULL
                              : ObjArrayKlass::cast(dst->klass())->element_klass();
    bool locked = RTGC::lock_refLink(dst);                          
    for (size_t i = 0; i < length; i++) {
      ITEM_T s_raw = src_p[i]; 
      oopDesc* item = CompressedOops::decode(s_raw);
      if (checkcast && item != NULL) {
        Klass* stype = item->klass();
        if (stype != bound && !stype->is_subtype_of(bound)) {
          memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*i);
          RTGC::unlock_refLink(locked);
          return false; 
        }
      }
      oopDesc* old = CompressedOops::decode(dst_p[i]);
      //dst_p[i] = s_raw; // ARRAYCOPY_DISJOINT
      if (item != NULL) RTGC::add_referrer(item, dst);
      if (old != NULL) RTGC::remove_referrer(old, dst);
    } 
    memmove((void*)dst_p, (void*)src_p, sizeof(ITEM_T)*length);
    RTGC::unlock_refLink(locked);
    return true;
  }

  static bool check_arraycopy_offsets(arrayOop s, int src_pos, arrayOop d,
                              int dst_pos, int length, TRAPS) {

    // Check is all offsets and lengths are non negative
    if (src_pos < 0 || dst_pos < 0 || length < 0) {
      // Pass specific exception reason.
      ResourceMark rm(THREAD);
      stringStream ss;
      if (src_pos < 0) {
        ss.print("arraycopy: source index %d out of bounds for object array[%d]",
                src_pos, s->length());
      } else if (dst_pos < 0) {
        ss.print("arraycopy: destination index %d out of bounds for object array[%d]",
                dst_pos, d->length());
      } else {
        ss.print("arraycopy: length %d is negative", length);
      }
      THROW_MSG_(vmSymbols::java_lang_ArrayIndexOutOfBoundsException(), ss.as_string(), false);
    }
    // Check if the ranges are valid
    if ((((unsigned int) length + (unsigned int) src_pos) > (unsigned int) s->length()) ||
        (((unsigned int) length + (unsigned int) dst_pos) > (unsigned int) d->length())) {
      // Pass specific exception reason.
      ResourceMark rm(THREAD);
      stringStream ss;
      if (((unsigned int) length + (unsigned int) src_pos) > (unsigned int) s->length()) {
        ss.print("arraycopy: last source index %u out of bounds for object array[%d]",
                (unsigned int) length + (unsigned int) src_pos, s->length());
      } else {
        ss.print("arraycopy: last destination index %u out of bounds for object array[%d]",
                (unsigned int) length + (unsigned int) dst_pos, d->length());
      }
      THROW_MSG_(vmSymbols::java_lang_ArrayIndexOutOfBoundsException(), ss.as_string(), false);
    }

    // Special case. Boundary cases must be checked first
    // This allows the following call: copy_array(s, s.length(), d.length(), 0).
    // This is correct, since the position is supposed to be an 'in between point', i.e., s.length(),
    // points to the right of the last element.
    if (length==0) {
      return false;
    }
    return true;
  }
};

#endif // __RTGC_ARRAY_HPP__
