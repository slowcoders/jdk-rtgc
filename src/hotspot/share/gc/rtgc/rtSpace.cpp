#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace rtHeapUtil;
using namespace RTGC;



bool rtHeapUtil::is_dead_space(oopDesc* obj) {
  Klass* klass = obj->klass();
  return klass->is_typeArray_klass() || klass == vmClasses::Object_klass();
}

void rtHeapUtil::ensure_alive_or_deadsapce(oopDesc* old_p) {
  assert(!to_obj(old_p)->isGarbageMarked() || is_dead_space(old_p), 
        "invalid pointer %p(%s) isClass=%d\n", 
        old_p, RTGC::getClassName(to_obj(old_p)), old_p->klass() == vmClasses::Class_klass());
}

