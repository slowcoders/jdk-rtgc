#include "precompiled.hpp"

#include "runtime/globals.hpp"
#include "utilities/debug.hpp"
#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "gc/shared/genCollectedHeap.hpp"
#include "gc/shared/weakProcessor.hpp"
#include "gc/shared/genOopClosures.inline.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

#include "rtRefProcessor.hpp"


class YoungRootClosure : public BasicOopIterateClosure {
  OopIterateClosure* _mark_closure;
public:

  YoungRootClosure() {}

  void init(OopIterateClosure* mark_closure) {
    _mark_closure = mark_closure;
  }

  bool scan_young_root(oop obj) {
    _has_young_ref = false;
    obj->oop_iterate(this);
    return _has_young_ref;
  }

  template <typename T> void do_oop_work(T* p) {
  T heap_oop = RawAccess<>::oop_load(p);
  if (!CompressedOops::is_null(heap_oop)) {
    oop obj = CompressedOops::decode_not_null(heap_oop);
    if (obj != NULL && !to_obj(obj)->isTrackable()) {
      this->_has_young_ref = true;
    }
  }

  virtual void do_oop(oop* o) {
    _mark_closure->do_oop(o);
    if (!_has_young_ref) do_oop_work(o);
  }

  virtual void do_oop(narrowOop* o) {
    _mark_closure->do_oop(o);
    if (!_has_young_ref) do_oop_work(o);
  }
};


