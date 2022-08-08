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
  GCObject* _base;
  RefTracer2 _fn;
  HugeArray<GCObject*>* _stack;

public:
  FieldIterator(oopDesc* p, RefTracer2 trace_fn, HugeArray<GCObject*>* stack)
      : _base(to_obj(p)), _fn(trace_fn), _stack(stack) {
  }

  virtual void do_oop(oop* o) { work_oop(RawAccess<>::oop_load(o)); }
  virtual void do_oop(narrowOop* o) { work_oop(RawAccess<>::oop_load(o)); }

  void work_oop(oop obj) {
    if (obj == NULL) return;
    // if (to_obj(obj)->isGarbageMarked()) return;
    if (!to_obj(obj)->isTrackable()) {
      if (!_base->isYoungRoot()) {
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
    GCObject* link = to_obj(obj);
    if (!link->isGarbageMarked()) {
      if (_fn(link, _base)) {
        _stack->push_back(link);
      }
    }
  }
};


void RuntimeHeap::scanInstanceGraph(GCObject* root, RefTracer2 trace, HugeArray<GCObject*>* stack) {
  oopDesc* p = cast_to_oop(root);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop> fi(p, trace, stack);
    p->oop_iterate(&fi);
  } else {
    FieldIterator<oop> fi(p, trace, stack);
    p->oop_iterate(&fi);
  }
}



