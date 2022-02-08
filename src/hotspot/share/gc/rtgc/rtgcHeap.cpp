#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "runtime/globals.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "gc/shared/genCollectedHeap.hpp"


using namespace RTGC;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

template <class T>
class OopIterator {
  oopDesc* _obj;
  OopMapBlock* _map;
  T* _field;
  int _cntMap;
  int _cntOop;

  OopIterator(oopDesc* obj) : _obj(obj) {
    Klass* klass = obj->klass();
    if (klass->is_objArray_klass()) {
      arrayOopDesc* array = (arrayOopDesc*)obj;
      _cntOop = array->length();
      _field = (T*)(((address)obj) + array->base_offset_in_bytes(T_OBJECT));
      _cntMap = 0;
    }
    else if (klass->is_instance_klass()) {
      InstanceKlass* objKlass = (InstanceKlass*)klass;
      _cntMap = objKlass->nonstatic_oop_map_count();
      if (--_cntMap < 0) {
        _cntOop = 0;
      } else {
        setOopMap(objKlass->start_of_nonstatic_oop_maps());
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
    _field = (T*)_obj->obj_field_addr<T>(map->offset());
    _cntOop = map->count();
  }

public:
  OopIterator() {}
  static void scanInstance(oopDesc* obj, RTGC::RefTracer trace) {
    GrowableArrayCHeap<OopIterator, mtGC> stack;
    stack.append(OopIterator(obj));
    OopIterator* it = &stack.at(0);
    while (true) {
      oopDesc* link = it->next();
      if (link == nullptr) {
        stack.pop();
        int len = stack.length();
        if (--len < 0) break;
        it = &stack.at(len);
      }
      else if (trace(to_obj(link))) {
          stack.append(OopIterator(link));
          it = &stack.at(stack.length() - 1);
      }
    }    
  }  
};


class RTGC_MarkClosure : public BasicOopIterateClosure {
public:
  GrowableArrayCHeap<oopDesc*, mtGC> ref_stak;
  int _cnt;
  int _cntLocal;

  RTGC_MarkClosure() : _cnt(0), _cntLocal(0) {}

  void do_work(oopDesc* obj) {
    if (obj != NULL) {
      ref_stak.append(obj);
      _cnt ++;
      if (!RTGC::isPublished(obj)) {
        _cntLocal ++;
      }
      // rtgc_log(LOG_OPT(0x100), "mark stack %d %p\n", _cnt++, obj);
    }
    //if (ref != NULL) RTGC::add_referrer(ref, _rookie);
  }

  virtual void do_oop(narrowOop* p) { do_work(CompressedOops::decode(*p)); }
  virtual void do_oop(oop*       p) { do_work(*p); }
};

class RtgcThreadLocalData {
  GrowableArrayCHeap<oopDesc*, mtGC> localObjects;
  // freeSpaces;

public:
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
      rtgc_log(LOG_OPT(0x100), "rookie %d, %s\n", ++cnt, obj->klass()->name()->bytes());
      p += obj->size();  // size() == sizeInBytes / sizeof(HeapWord);
    }
  }

  void markStack(Thread* thread) {
    ResourceMark rm;
    RTGC_MarkClosure c;
    thread->oops_do(&c, NULL);
    rtgc_log(LOG_OPT(0x100), "local root marked %d(%d)\n", c._cnt, c._cntLocal);
  }

  void collectTlab(Thread* thread) {
    int end = localObjects.length();
    int dst = 0;
    for (int idx = 0; idx < end; idx ++) {
      oopDesc* obj = localObjects.at(idx);
      if (!RTGC::collectGarbage(obj) && dst != idx
      &&  !RTGC::isPublished(obj)) {
        localObjects.at(dst++) = obj;
      }
    }
    if (dst != end) {
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
  HeapWord* mem = tld->try_alloc_tlab(word_size);
  if (mem == nullptr) {
    tld->markStack(thread);
    //tld->collectTlab(thread);
  }
  return NULL;
}

void RTGC::scanInstance(GCObject* root, RTGC::RefTracer trace) {
  oopDesc* obj = cast_to_oop(root);
#ifdef _LP64
  if (UseCompressedOops) {
    OopIterator<narrowOop>::scanInstance(obj, trace);
  } else {
    OopIterator<oop>::scanInstance(obj, trace);
  }
#else
  OopIterator<oop>::scanInstance(obj, trace);
#endif
}

void RTGC::adjust_pointers(oopDesc* ref, void* young_gen_end) {
  if (!RTGC::debugOptions->opt1) return;
  if (!((GenCollectedHeap*)Universe::heap())->is_in_young(ref)) {
    rtgc_log(LOG_OPT(1), "not young obj %p\n", ref);// || (obj->hasReferrer() && !obj->_hasMultiRef));
    return;
  }
  GCObject* obj = to_obj(ref);

  void* moved_to = ref->mark().decode_pointer();
  if (!ref->is_gc_marked() || moved_to == NULL) {
    rtgc_log(LOG_OPT(1), "not marked %p\n", ref);// || (obj->hasReferrer() && !obj->_hasMultiRef));
    return;
  }
  const bool CHECK_GARBAGE = true;

  if (!obj->hasReferrer()) {
    rtgc_log(LOG_OPT(1), "no ref in %p\n", ref);// || (obj->hasReferrer() && !obj->_hasMultiRef));
    return;
  }
  if (obj->_hasMultiRef) {
    ReferrerList* referrers = obj->getReferrerList();
    for (int idx = 0; idx < referrers->size(); ) {
      oopDesc* oop = cast_to_oop(referrers->at(idx));
      if (CHECK_GARBAGE && // new_obj == (void*)0xbaadbabebaadbabc) {
          !oop->is_gc_marked() && ((GenCollectedHeap*)Universe::heap())->is_in_young(oop)) {// && oop < young_gen_end) {
        rtgc_log(LOG_OPT(1), "remove garbage ref %p in %p(%p)\n", oop, obj, moved_to);
        referrers->removeFast(idx);
        continue;
      }
      GCObject* new_obj = (GCObject*)oop->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(1), "ref moved %p->%p in %p\n", oop, new_obj, obj);
        referrers->at(idx) = new_obj;
      }
      idx ++;
    }

    if (referrers->size() < 2) {
      obj->_hasMultiRef = false;
      if (referrers->size() == 0) {
        obj->_refs = 0;
      }
      else {
        GCObject* remained = referrers->at(0);
        obj->_refs = _pointer2offset(remained, &obj->_refs);
      }
      _rtgc.gRefListPool.delete_(referrers);
    }
  }
  else {
    oopDesc* oop = cast_to_oop(_offset2Object(obj->_refs, &obj->_refs));
    if (CHECK_GARBAGE && // new_obj == (void*)0xbaadbabebaadbabc) {//
        !oop->is_gc_marked()  && ((GenCollectedHeap*)Universe::heap())->is_in_young(oop)) {// && oop < young_gen_end) {
      rtgc_log(LOG_OPT(1), "remove garbage ref %p in %p\n", oop, obj);
      obj->_refs = 0;
    }
    else {
      GCObject* new_obj = (GCObject*)oop->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(1), "ref moved %p->%p in %p\n", oop, new_obj, obj);
        obj->_refs = _pointer2offset(new_obj, &obj->_refs);
      }
    }
  }
}

