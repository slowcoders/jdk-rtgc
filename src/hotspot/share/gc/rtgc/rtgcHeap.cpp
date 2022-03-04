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
#include "gc/shared/genCollectedHeap.hpp"
#include "gc/serial/markSweep.inline.hpp"


namespace RTGC {
  GrowableArrayCHeap<oop, mtGC> g_promted_trackables;
  GrowableArrayCHeap<oop, mtGC> g_young_roots;
  static Thread* gcThread = NULL;
  static int g_cntTrackable = 0;
};
using namespace RTGC;

static bool is_narrow_oop_mode() {
#ifdef _LP64
  return UseCompressedOops;
#endif
  return false;
}

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

static bool is_java_reference(oopDesc* obj) {
  return (obj->klass()->id() == InstanceRefKlassID);
}

static bool has_young_referent(oopDesc* obj) {
  if (!is_java_reference(obj)) return false;

  void* referent_addr = java_lang_ref_Reference::referent_addr_raw(obj);
  oop referent;
  if (is_narrow_oop_mode()) {
    referent = RawAccess<>::oop_load((narrowOop*)referent_addr);
  }
  else {
    referent = RawAccess<>::oop_load((oop*)referent_addr);
  }
  return referent != NULL && !to_obj(referent)->isTrackable();
}

template <class T>
class FieldIterator {
  oopDesc* _base;
  OopMapBlock* _map;
  T* _field;
  int _cntMap;
  int _cntOop;

public:
  FieldIterator(oopDesc* p) : _base(p) {
    Klass* klass = p->klass();
    if (klass->is_objArray_klass()) {
      arrayOopDesc* array = (arrayOopDesc*)p;
      _cntOop = array->length();
      _field = (T*)(((address)p) + array->base_offset_in_bytes(T_OBJECT));
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

  void setOopMap(OopMapBlock* map) {
    _map = map;
    _field = (T*)_base->obj_field_addr<T>(map->offset());
    _cntOop = map->count();
  }

  void iterate_static_pointers(OopClosure* closure) {
    InstanceKlass* klass = (InstanceKlass*)java_lang_Class::as_Klass(_base);
    if (klass == NULL || !klass->is_loaded()) return;

    rtgc_log(LOG_OPT(11), "tracing klass %p(%s)\n", _base, klass->name()->bytes());
    for (JavaFieldStream fs(klass); !fs.done(); fs.next()) {
      if (fs.access_flags().is_static()) {
        fieldDescriptor& fd = fs.field_descriptor();
        if (fd.field_type() == T_OBJECT) {
          T* field = _base->obj_field_addr<T>(fd.offset());
          closure->do_oop(field);
        }
      }
    }
  }

  oopDesc* next() {
    while (true) {
      while (--_cntOop >= 0) {
        T heap_oop = RawAccess<>::oop_load(_field++);
        if (!CompressedOops::is_null(heap_oop)) {
          oop obj = CompressedOops::decode_not_null(heap_oop);
          return obj;
        }
      }
      if (--_cntMap < 0) break;
      setOopMap(_map + 1);
    }
    return NULL;
  }

public:
  FieldIterator() {}
  void iterate_pointers(OopClosure* closure) {
    while (true) {
      while (--_cntOop >= 0) {
        closure->do_oop(_field++);
      }
      if (--_cntMap < 0) break;
      setOopMap(_map + 1);
    }
    if (_base->klass() == vmClasses::Class_klass()) {
      iterate_static_pointers(closure);
    }
  }

  static void scanInstance(oopDesc* p, RTGC::RefTracer trace) {
    GrowableArrayCHeap<FieldIterator, mtGC> stack;
    stack.append(FieldIterator(p));
    FieldIterator* it = &stack.at(0);
    while (true) {
      oopDesc* link = it->next();
      if (link == nullptr) {
        stack.pop();
        int len = stack.length();
        if (--len < 0) break;
        it = &stack.at(len);
      }
      else if (trace(to_obj(link))) {
          stack.append(FieldIterator(link));
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

  void do_work(oopDesc* p) {
    if (p != NULL) {
      ref_stak.append(p);
      _cnt ++;
      if (!RTGC::isPublished(p)) {
        _cntLocal ++;
      }
      // rtgc_log(LOG_OPT(0x100), "mark stack %d %p\n", _cnt++, p);
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
    HeapWord* t_p = tlab.start();
    HeapWord* end = tlab.top();
    int cnt = 0;
    while (t_p < end) {
      oopDesc* p = cast_to_oop(t_p);
      if (!RTGC::collectGarbage(p)
      &&  !RTGC::isPublished(p)) {
        localObjects.append(p);
      }
      rtgc_log(LOG_OPT(12), "rookie %d, %s\n", ++cnt, p->klass()->name()->bytes());
      t_p += p->size();  // size() == sizeInBytes / sizeof(HeapWord);
    }
  }

  void markStack(Thread* thread) {
    ResourceMark rm;
    RTGC_MarkClosure c;
    thread->oops_do(&c, NULL);
    rtgc_log(LOG_OPT(12), "local root marked %d(%d)\n", c._cnt, c._cntLocal);
  }

  void collectTlab(Thread* thread) {
    int end = localObjects.length();
    int dst = 0;
    for (int idx = 0; idx < end; idx ++) {
      oopDesc* p = localObjects.at(idx);
      if (!RTGC::collectGarbage(p) && dst != idx
      &&  !RTGC::isPublished(p)) {
        localObjects.at(dst++) = p;
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


HeapWord* rtHeap::allocate_tlab(Thread* thread, const size_t word_size) {
  RtgcThreadLocalData* tld = RtgcThreadLocalData::data(thread);
  HeapWord* mem = tld->try_alloc_tlab(word_size);
  if (mem == nullptr) {
    tld->markStack(thread);
    //tld->collectTlab(thread);
  }
  return NULL;
}

void RTGC::scanInstance(GCObject* root, RTGC::RefTracer trace) {
  oopDesc* p = cast_to_oop(root);
#ifdef _LP64
  if (UseCompressedOops) {
    FieldIterator<narrowOop>::scanInstance(p, trace);
  } else {
    FieldIterator<oop>::scanInstance(p, trace);
  }
#else
  FieldIterator<oop>::scanInstance(p, trace);
#endif
}

class Ref2Tracer : public OopClosure {
  RTGC::RefTracer2 _trace;
  void* _param;
public:  
  Ref2Tracer(RTGC::RefTracer2 trace, void* param) {
    _trace = trace;
    _param = param;
  }

  template <class T>
  void do_oop_work(T* p) {
    T heap_oop = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(heap_oop)) {
      oop obj = CompressedOops::decode_not_null(heap_oop);
      _trace(to_obj(obj), _param);
    }    
  }

  virtual void do_oop(oop* p) { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p); }

};

void RTGC::iterateReferents(GCObject* root, RTGC::RefTracer2 trace, void* param) {
  oopDesc* p = cast_to_oop(root);
  Ref2Tracer tracer(trace, param);
  if (is_narrow_oop_mode()) {
    FieldIterator<narrowOop> it(p);
    it.iterate_pointers(&tracer);
  }
  else {
    FieldIterator<oop> it(p);
    it.iterate_pointers(&tracer);
  }
}

void rtHeap::adjust_tracking_pointers(oopDesc* ref, bool remove_garbage) {
  GCObject* obj = to_obj(ref);

  precond(!remove_garbage || ref->is_gc_marked());
  if (!obj->hasReferrer()) {
    return;
  }

  void* moved_to = ref->mark().decode_pointer();
  const bool CHECK_GARBAGE = remove_garbage;

  if (obj->hasMultiRef()) {
    ReferrerList* referrers = obj->getReferrerList();
    for (int idx = 0; idx < referrers->size(); ) {
      oopDesc* oop = cast_to_oop(referrers->at(idx));
      if (CHECK_GARBAGE && !oop->is_gc_marked()) {
        rtgc_log(LOG_OPT(11), "remove garbage ref %p in %p(move to->%p)\n", oop, obj, moved_to);
        referrers->removeFast(idx);
        continue;
      }
      GCObject* new_obj = (GCObject*)oop->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(11), "ref moved %p->%p in %p\n", 
          oop, new_obj, obj);
        referrers->at(idx) = new_obj;
      }
      idx ++;
    }

    if (CHECK_GARBAGE && referrers->size() < 2) {
      obj->setHasMultiRef(false);
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
    if (CHECK_GARBAGE && !oop->is_gc_marked()) {
      rtgc_log(LOG_OPT(11), "remove garbage ref %p in %p(move to->%p)\n", oop, obj, moved_to);
      obj->_refs = 0;
    }
    else {
      GCObject* new_obj = (GCObject*)oop->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(11), "ref moved %p->%p in %p\n", 
          oop, new_obj, obj);
        obj->_refs = _pointer2offset(new_obj, &obj->_refs);
      }
    }
  }
}



static GCObject* findNextUntrackable(GCNode* obj) {
  precond(obj != NULL && !obj->isTrackable());
  obj = obj->_nextUntrackable;
  precond(obj != NULL); 
  while (obj->isTrackable()) {
    GCNode* next = obj->_nextUntrackable;
    precond(next != NULL);
    obj->_nextUntrackable = NULL;
    obj = next;
  }
  return (GCObject*)obj;
}

#ifdef ASSERT
RTGC::mark_dead_space(void* p) {
  ((GCNode*)p)->markGarbage();
}
#endif

void rtHeap::refresh_young_roots(bool is_object_moved) {
  //if (!REF_LINK_ENABLED) return;
  debug_only(if (gcThread == NULL) gcThread = Thread::current();)
  precond(gcThread == Thread::current());

  if (g_young_roots.length() == 0) {
    rtgc_log(LOG_OPT(11), "refresh_young_roots 0->0\n"); 
    return;
  }
  oop* src = &g_young_roots.at(0);
  oop* dst = src;
  for (int i = g_young_roots.length(); --i >= 0; src++) {
    oopDesc* p = *src;
#ifdef ASSERT    
    if (!to_obj(p)->isYoungRoot()) {
      assert(to_obj(p)->isGarbageMarked(), "YGRoot Err %p\n", p);
    } else
#endif    
    if (p->is_gc_marked()) {
      oopDesc* p2 = p->forwardee();
      postcond(!is_object_moved || p2 != NULL);
      if (p2 == NULL || p2 == (void*)0xbaadbabebaadbabc) p2 = p;
      if (dst != src || p != p2) {
        *dst = p2;
      }
      dst ++;
    }
  }
  int cnt_young_root = dst - &g_young_roots.at(0);
  rtgc_log(LOG_OPT(11), "refresh_young_roots %d->%d\n", 
      g_young_roots.length(), cnt_young_root);
  g_young_roots.trunc_to(cnt_young_root);
}

void RTGC::add_young_root(oopDesc* p) {
  GCObject* obj = to_obj(p);
  precond(obj->isTrackable());
  obj->markYoungRoot();
  g_young_roots.append(p);
  rtgc_log(LOG_OPT(11), "add young root %p root=%d\n", obj, g_young_roots.length());
}

bool rtHeap::is_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isTrackable();
}

void rtHeap::destrory_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  if (obj->isTrackable()) {
    obj->removeAllReferrer();
    debug_only(g_cntTrackable --);
  }
}

void rtHeap::mark_active_trackable(oopDesc* p) {
  fatal("mark_active_trackable is not thread safe!!");
}

void rtHeap::mark_empty_trackable(oopDesc* p) {
  assert(SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread(), 
      "is not a deadspace?");
  rtgc_log(LOG_OPT(9), "mark_empty_trackable %p\n", p);
  GCObject* obj = to_obj(p);
  obj->markTrackable();
  debug_only(g_cntTrackable++);
}

void rtHeap::mark_pending_trackable(oopDesc* old_p, void* new_p) {
  //if (!REF_LINK_ENABLED) return;
  rtgc_log(LOG_OPT(9), "mark_pending_trackable %p (move to -> %p)\n", old_p, new_p);
  precond((void*)old_p->forwardee() == new_p);
  GCObject* obj = to_obj(old_p);
  obj->markTrackable();
  debug_only(g_cntTrackable++);
  g_promted_trackables.append((oopDesc*)new_p);
}

void rtHeap::mark_promoted_trackable(oopDesc* old_p, oopDesc* new_p) {
  //if (!REF_LINK_ENABLED) return;
  // 이미 객체가 복사된 상태이므로, 둘 다 marking 되어야 한다.
  // old_p 를 marking 하여, young_roots 에서 제거될 수 있도록 하고,
  // new_p 를 marking 하여, young_roots 에 등록되지 않도록 한다.
  rtgc_log(LOG_OPT(9), "mark_promoted_trackable %p(moved to %p)\n", old_p, new_p);
  to_obj(old_p)->markTrackable();
  to_obj(new_p)->markTrackable();
  debug_only(g_cntTrackable++);
  g_promted_trackables.append(new_p);
}

void rtHeap::flush_trackables() {
  //if (!REF_LINK_ENABLED) return;
  const int count = g_promted_trackables.length();
  if (count == 0) return;

  rtgc_log(LOG_OPT(11), "flush_trackables %d\n", count);
  oop* pOop = &g_promted_trackables.at(0);
  for (int i = count; --i >= 0; ) {
    oopDesc* p = *pOop++;
    RTGC::iterateReferents(to_obj(p), (RefTracer2)add_referrer_unsafe, p);
    if (!to_obj(p)->isYoungRoot() && has_young_referent(p)) {
      add_young_root(p);
    }
  }
  g_promted_trackables.trunc_to(0);
}

class OldAnchorAdustPointer : public BasicOopIterateClosure {
  OopIterateClosure* _closure;
public:
  int cnt_young_ref = 0;

  OldAnchorAdustPointer(OopIterateClosure* closure) {
    _closure = closure;
    cnt_young_ref = 0;
  }

  template <typename T>
  void do_oop_work(T* p) {
    T heap_oop = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(heap_oop)) {
      oop ref = CompressedOops::decode_not_null(heap_oop);
      _closure->do_oop(p);
      rtgc_log(LOG_OPT(11), "iterate young_ref %p->%p\n", (void*)ref, (void*)*p);
      if (!to_obj(ref)->isTrackable()) {
        cnt_young_ref ++;
      }
    }
  }

  virtual void do_oop(narrowOop* p) { do_oop_work(p); }
  virtual void do_oop(oop*       p) { do_oop_work(p); }
};

void rtHeap::iterate_young_roots(OopIterateClosure* closure) {
  /**
   * 참고) promoted object 에 대한 adjust_pointer 실행 전 상태.
   */

  if (g_young_roots.length() == 0) return;
  OldAnchorAdustPointer ap(closure);
  oop* src = &g_young_roots.at(0);
  oop* dst = src;
  for (int i = g_young_roots.length(); --i >= 0; src++) {
    oopDesc* anchor = *src;
    
    rtgc_log(LOG_OPT(11), "iterate anchor %p\n", (void*)anchor);
    bool is_root;
    if (is_java_reference(anchor)) {
      anchor->oop_iterate(closure);
      is_root = has_young_referent(anchor);
    }
    else if (is_narrow_oop_mode()) {
      FieldIterator<narrowOop> it(anchor);
      it.iterate_pointers(&ap);
      is_root = ap.cnt_young_ref > 0;
    }
    else {
      FieldIterator<oop> it(anchor);
      it.iterate_pointers(&ap);
      is_root = ap.cnt_young_ref > 0;
    }
    if (!is_root) {
      to_obj(anchor)->unmarkYoungRoot();
    }
    else {
      if (dst != src) {
        *dst = anchor;
      }
      dst ++;
    }
  }
  int cnt_young_root = dst - &g_young_roots.at(0);
  rtgc_log(LOG_OPT(1), "iterate_young_roots done %d->%d\n", 
      g_young_roots.length(), cnt_young_root);
  g_young_roots.trunc_to(cnt_young_root);
}

void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.length(), full_gc); 
}

bool rtHeap::is_alive(oopDesc* p) {
  fatal("not impl");
  return true;
}