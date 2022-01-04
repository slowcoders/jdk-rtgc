
#ifndef __RTGC_ARRAY_HPP__
#define __RTGC_ARRAY_HPP__

#include "gc/rtgc/RTGC.hpp"

//extern volatile int ENABLE_RTGC_STORE_HOOK;
void RTGC_oop_arraycopy2();

struct RTGCArray {

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
