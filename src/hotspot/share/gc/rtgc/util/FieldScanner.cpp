#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"

#include "gc/rtgc/rtgcGlobals.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace RTGC;

template <class T, bool isTenured>
class FieldIterator : public BasicOopIterateClosure {
  GCObject* _base;
  RefTracer2 _fn;
  HugeArray<GCObject*>* _stack;

public:
  bool _hasForwardedPointers;

  FieldIterator(oopDesc* p, RefTracer2 trace_fn, HugeArray<GCObject*>* stack)
      : _base(to_obj(p)), _fn(trace_fn), _stack(stack) {
        if (!isTenured) {
          this->_hasForwardedPointers = to_obj(p)->isYoungRoot()
            && !to_obj(p)->isDirtyReferrerPoints();
        }
  }

  virtual void do_oop(oop* o) { work_oop(RawAccess<>::oop_load(o)); }
  virtual void do_oop(narrowOop* o) { work_oop(RawAccess<>::oop_load(o)); }
  virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }

  void work_oop(oop obj) {
    if (obj == NULL) return;

    GCObject* link = to_obj(obj);
    if (isTenured) rtgc_debug_log(to_obj(_base), "FieldIterator %p->%p(g=%d)\n", _base, (void*)obj, link->isGarbageMarked());

    if (link->isGarbageMarked()) {
      return;
    }

    if (isTenured) {
      if (!rtHeap::is_alive(obj, false)) {
        if (!link->isTrackable()) {
          link->clearAnchorList();
        }
        return;
      }
    } else if (_hasForwardedPointers) {
      if (link->isGarbageMarked()) {
        return;
      }
      /**
       * 참고) base 가 young_root 이고, 하위 YG reference 에 대한 복사가 종료되었으면,
       * is_gc_marked() 가 false 이다.
       */
    } else if (!link->isTrackable()) {
      if (link->isGarbageMarked()) {
        return;
      }
      if (!obj->is_gc_marked()) {
        link->clearAnchorList();
        return;
      }
      oop p = obj->forwardee();
      rt_assert(p != NULL);
      link = to_obj(p);
    }
    // rt_assert(p != NULL);
    if (_fn(link, _base)) {
      _stack->push_back(link);
    }
  }
};


void RuntimeHeap::scanInstanceGraph(GCObject* root, RefTracer2 trace, HugeArray<GCObject*>* stack, bool isTenured) {
  rt_assert(root->isTrackable());

  oopDesc* p = cast_to_oop(root);
  if (!isTenured) {
    if (RTGC::is_narrow_oop_mode) {
      FieldIterator<narrowOop, false> fi(p, trace, stack);
      p->oop_iterate(&fi);
    } else {
      FieldIterator<oop, false> fi(p, trace, stack);
      p->oop_iterate(&fi);
    }
  } else {
    if (RTGC::is_narrow_oop_mode) {
      FieldIterator<narrowOop, true> fi(p, trace, stack);
      p->oop_iterate(&fi);
    } else {
      FieldIterator<oop, true> fi(p, trace, stack);
      p->oop_iterate(&fi);
    }
  }
}


