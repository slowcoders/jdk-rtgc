#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace RTGC;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

template <class T>
class FieldIterator : public BasicOopIterateClosure {
  oopDesc* _base;
  void* _fn;
  void* _param;

public:
  FieldIterator(oopDesc* p, void* trace_fn, void* param) : _base(p), _fn(trace_fn), _param(param) {
  }

  virtual void do_oop(oop* o) { work_oop(RawAccess<>::oop_load(o)); }
  virtual void do_oop(narrowOop* o) { work_oop(RawAccess<>::oop_load(o)); }

  void work_oop(oop obj) {
    if (obj == NULL) return;
    // if (to_obj(obj)->isGarbageMarked()) return;
    if (!to_obj(obj)->isTrackable()) {
      if (!to_obj(_base)->isYoungRoot()) {
        if (!obj->is_gc_marked()) {
          rtgc_debug_log(to_obj(_base), "FieldIterator %p->%p\n", _base, (void*)obj);
          return;
        }
        oop p = obj->forwardee();
        if (p != NULL) {
          obj = p;
        }
      }
    }
    // precond(p != NULL);
    ((RefTracer2)_fn)(to_obj(obj), to_obj(_base));
  }

  template <int args>
  static void scanInstanceGraph(oopDesc* p, void* trace_fn, void* param) {
    FieldIterator fi(p, trace_fn, param);
    p->oop_iterate(&fi);
  }    
};

void RTGC::scanInstanceGraph(GCObject* root, RTGC::RefTracer1 trace) {
  fatal("deprecated");
  oopDesc* p = cast_to_oop(root);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop>::scanInstanceGraph<1>(p, (void*)trace, p);
  } else {
    FieldIterator<oop>::scanInstanceGraph<1>(p, (void*)trace, p);
  }
}

void RTGC::scanInstanceGraph(GCObject* root, RTGC::RefTracer2 trace) {
  oopDesc* p = cast_to_oop(root);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop>::scanInstanceGraph<2>(p, (void*)trace, NULL);
  } else {
    FieldIterator<oop>::scanInstanceGraph<2>(p, (void*)trace, NULL);
  }
}

void RTGC::scanInstanceGraph(GCObject* root, RTGC::RefTracer3 trace, void* param) {
  fatal("deprecated");
  oopDesc* p = cast_to_oop(root);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop>::scanInstanceGraph<3>(p, (void*)trace, param);
  } else {
    FieldIterator<oop>::scanInstanceGraph<3>(p, (void*)trace, param);
  }
}


