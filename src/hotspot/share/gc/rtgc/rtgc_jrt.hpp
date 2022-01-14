#ifndef __RTGC_JRT_HPP__
#define __RTGC_JRT_HPP__

#include "oops/accessDecorators.hpp"
#include "oops/compressedOops.hpp"

oopDesc* rtgc_oop_array_xchg_0(arrayOopDesc* array, narrowOop* addr, oopDesc* new_value);
oopDesc* rtgc_oop_array_xchg_3(arrayOopDesc* array, narrowOop* addr, oopDesc* new_value);
oopDesc* rtgc_oop_array_xchg_8(arrayOopDesc* array, oop* addr, oopDesc* new_value);

oopDesc* rtgc_oop_xchg_0(oopDesc* base, narrowOop* addr, oopDesc* new_value);
oopDesc* rtgc_oop_xchg_3(oopDesc* base, narrowOop* addr, oopDesc* new_value);
oopDesc* rtgc_oop_xchg_8(oopDesc* base, oop* addr, oopDesc* new_value);

oopDesc* rtgc_oop_cmpxchg_0(oopDesc* base, narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value);
oopDesc* rtgc_oop_cmpxchg_3(oopDesc* base, narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value);
oopDesc* rtgc_oop_cmpxchg_8(oopDesc* base, oop* addr, oopDesc* cmp_value, oopDesc* new_value);

#endif // __RTGC_JRT_HPP__
