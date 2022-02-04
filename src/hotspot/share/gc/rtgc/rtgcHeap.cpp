#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace RTGC;

template <T>
class OopIterator {
    oopDesc* _obj;
    OopMapBlock* _map;
    T* _field;
    int cntMap;
    int cntOop;

    OopMapBlock* const _end_map;

    OopIterator(oopDesc* obj) : _obj(obj) {
        Klass* klass = obj->klass();
        if (klass->is_objArray_klass()) {
            arrayOopDesc* array = (arrayOopDesc)obj;
            _cntOop = array->length();
            _field = (T*)(((address)obj) + array->base_offset_in_bytes(T_OBJECT));
            _cntMap = 0;
        }
        else if (klass->is_instance_klass()) {
            _cntMap = obj->klass()->nonstatic_oop_map_count();
            if (_cntMap == 0) {
                _cntOop = 0;
            } else {
                setOopMap(obj->klass()->start_of_nonstatic_oop_maps());
            }
        }
        else {
            _cntMap = _cntOop = 0;
        }
    }

    oopDesc* next() {
        while (true) {
            while (--_cntOop >= 0) {
                oopDesc* obj = CompressedOops::decode(*_field++);
                if (obj != nullptr) return obj;
            }
            if (--_cntMap < 0) break;
            setOopMap(_map + 1);
        }
        return nullptr;
    }
    void setOopMap(OopMapBlock* map) {
        _map = map;
        _field = (T*)obj->obj_field_addr<T>(map->offset());
        _cntOop = map->count();
    }
};


class RTGC_MarkClosure : public BasicOopIterateClosure {
  GrowableArrayCHeap<oopDesc*, mtGC> ref_stak;
  int _cnt;

  void do_work(oopDesc* obj) {
    if (obj != NULL) {
      ref_stak.append(obj);
      // rtgc_log(RTGC::debugOptions->opt1, "mark stack %d %p\n", _cnt++, obj);
    }
    //if (ref != NULL) RTGC::add_referrer(ref, _rookie);
  }

public:
  RTGC_MarkClosure() : _cnt(0) {}
  virtual void do_oop(narrowOop* p) { do_work(CompressedOops::decode(*p)); }
  virtual void do_oop(oop*       p) { do_work(*p); }

};

class RtgcThreadLocalData {
private:
  GrowableArrayCHeap<oopDesc*, mtGC> localObjects;
  // freeSpaces;

  RtgcThreadLocalData() {}

  static RtgcThreadLocalData* data(Thread* thread) {
    return thread->gc_data<RtgcThreadLocalData>();
  }

  void registerLocalRookies(ThreadLocalAllocBuffer& tlab) {
    HeapWord* p = tlab.start();
    HeapWord* end = tlab.top();
    int cnt = 0;
    while (p < end) {
      oopDesc* obj = cast_to_oop(p);
      if (!RTGC::collectGarbage(obj)
      &&  !RTGC::isPublished(obj)) {
        localObjects.append(obj);
      }
      rtgc_log(RTGC::debugOptions->opt1, "rookie %d, %s\n", ++cnt, obj->klass()->name()->bytes());
      p += obj->size();  // size() == sizeInBytes / sizeof(HeapWord);
    }
  }

  void markStack(Thread* thread) {
    ResourceMark rm;
    RTGC_MarkClosure c;
    thread->oops_do(&c, NULL);
  }

  void collectTlab(Thread* thread) {
    int end = localObjects.size();
    int dst = 0;
    for (int idx = 0; idx < end; idx ++) {
      oopDesc* obj = localObjects.at(idx);
      if (!RTGC::collectGarbage(obj) && dst != idx
      &&  !RTGC::isPublished(obj)) {
        localObjects.at(dst++) = obj;
      }
    }
    if (dst != idx) {
      localObjects.trunc_to(dst);
    }
    registerLocalRookies(thread->tlab());
  }

  HeapWord* try_alloc_tlab(const size_t word_size) {
    return nullptr;
  }

};


HeapWord* RTGC::allocate_tlab(Thread* thread, const size_t word_size) {
  RtgcThreadLocalData* tld = RtgcThreadLocalData::data(thread);
  HeapWord* mem = try_alloc_tlab(word_size);
  if (mem == nullptr) {
    tld->markStack(thread);
    tld->collectTlab(thread);
  }
  return NULL;
}

void RTGC::scanInstance(GCObject* obj, RTGC::RefTracer trace) {
  GrowableArrayCHeap<OopIterator, mtGC> stack;
  stack.append(cast_to_oop(obj));
  OopIterator* it = &stack.last();
  while (true) {
      GCObject* link = it->next();
      if (link == nullptr) {
          _traceStack.pop();
          if (_traceStack.empty()) break;
          it = &_traceStack.back();
      }
      else if (trace(link)) {
          _traceStack.append(link);
          it = &_traceStack.back();
      }
  }    
}

