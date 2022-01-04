#ifndef __RTGC_JRT_HPP__
#define __RTGC_JRT_HPP__

#include "oops/accessDecorators.hpp"
#include "oops/compressedOops.hpp"

oop rtgc_oop_xchg_0(oop base, volatile narrowOop* p, oop new_value);
oop rtgc_oop_xchg_3(oop base, volatile narrowOop* p, oop new_value);
oop rtgc_oop_xchg_8(oop base, volatile oop* p, oop new_value);

oop rtgc_oop_cmpxchg_0(oop base, volatile narrowOop* p, oop new_value);
oop rtgc_oop_cmpxchg_3(oop base, volatile narrowOop* p, oop new_value);
oop rtgc_oop_cmpxchg_8(oop base, volatile oop* p, oop new_value);

#endif // __RTGC_JRT_HPP__
