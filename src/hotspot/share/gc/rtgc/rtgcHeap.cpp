#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "gc/shared/genCollectedHeap.hpp"


namespace RTGC {
  class PsuedoYoungRoot : public GCNode {
  public:
    PsuedoYoungRoot() {
      clear();
    }
  } g_young_root_tail;
  GrowableArrayCHeap<oopDesc*, mtGC> g_promted_trackables;
  static Thread* gcThread = NULL;
	static int cnt_young_root = 0;
  static GCNode* volatile g_young_root_q = &g_young_root_tail;
};
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
    //rtgc_log(LOG_OPT(5), "scan %p: at _field[%p] count %d\n", _obj, _field, _cntOop);
  }

public:
  OopIterator() {}

  static void iterateReferents(oopDesc* obj, RTGC::RefTracer2 trace, void* param) {
    OopIterator it(obj);
    for (oopDesc* oop; (oop = it.next()) != NULL; ) {
      trace(to_obj(oop), param);
    }
    if (obj->klass() == vmClasses::Class_klass()) {
      InstanceKlass* klass = (InstanceKlass*)java_lang_Class::as_Klass(obj);
      if (klass != NULL && klass->is_loaded()) {
        rtgc_log(LOG_OPT(5), "tracing klass %p\n", obj);
        for (JavaFieldStream fs(klass); !fs.done(); fs.next()) {
          if (fs.access_flags().is_static()) {
            fieldDescriptor& fd = fs.field_descriptor();
            if (fd.field_type() == T_OBJECT) {
              T* field = obj->obj_field_addr<T>(fd.offset());
              oopDesc* ref = CompressedOops::decode(*field);
              if (ref != NULL) {
                trace(to_obj(ref), param);
              }
            }
          }
        }
      }
    }  
  }

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

void RTGC::iterateReferents(GCObject* root, RTGC::RefTracer2 trace, void* param) {
  oopDesc* obj = cast_to_oop(root);
#ifdef _LP64
  if (UseCompressedOops) {
    OopIterator<narrowOop>::iterateReferents(obj, trace, param);
  } else {
    OopIterator<oop>::iterateReferents(obj, trace, param);
  }
#else
  OopIterator<oop>::iterateReferents(obj, trace, param);
#endif
}

void RTGC::adjust_pointers(oopDesc* ref) {
  GCObject* obj = to_obj(ref);

  precond(ref->is_gc_marked());

  if (!REF_LINK_ENABLED) return;

  void* moved_to = ref->mark().decode_pointer();
  const bool CHECK_GARBAGE = true;

  if (!obj->hasReferrer()) {
    return;
  }
  if (obj->hasMultiRef()) {
    ReferrerList* referrers = obj->getReferrerList();
    for (int idx = 0; idx < referrers->size(); ) {
      oopDesc* oop = cast_to_oop(referrers->at(idx));
      if (CHECK_GARBAGE && !oop->is_gc_marked()) {
        rtgc_log(LOG_OPT(1), "remove garbage ref %p in %p(move to->%p)\n", oop, obj, moved_to);
        referrers->removeFast(idx);
        continue;
      }
      GCObject* new_obj = (GCObject*)oop->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(1), "ref moved %p->%p in %p\n", 
          oop, new_obj, obj);
        referrers->at(idx) = new_obj;
      }
      idx ++;
    }

    if (referrers->size() < 2) {
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
      rtgc_log(LOG_OPT(1), "remove garbage ref %p in %p(move to->%p)\n", oop, obj, moved_to);
      obj->_refs = 0;
    }
    else {
      GCObject* new_obj = (GCObject*)oop->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(1), "ref moved %p->%p in %p\n", 
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

void RTGC::refresh_young_roots() {
  if (!REF_LINK_ENABLED) return;
  debug_only(if (gcThread == NULL) gcThread = Thread::current();)
  precond(gcThread == Thread::current());

	int young_root = 0;
  int cnt_garbage = 0;
  rtgc_log(LOG_OPT(6), "refresh_young_roots started %d\n", 
    cnt_young_root);

  PsuedoYoungRoot header;
  GCNode* prev = &header;
  GCNode* obj = g_young_root_q;
  while ((GCNode*)obj != &g_young_root_tail) {
    oopDesc* ptr = cast_to_oop(obj);
    rtgc_log(LOG_OPT(6), "check young root %p\n", obj);
    if (!ptr->is_gc_marked()) {
      rtgc_log(LOG_OPT(8), "young garbage %p\n", ptr);
      debug_only(cnt_garbage++;)
      ((GCObject*)obj)->removeAllReferrer();
    }
    else if (!obj->isTrackable() && obj->hasReferrer()) {
      debug_only(young_root++;)
      obj = to_obj(ptr->forwardee());
      postcond(obj != NULL);
      prev->_nextUntrackable = obj;
      prev = obj;
    }
    obj = obj->_nextUntrackable;
  }
  prev->_nextUntrackable = &g_young_root_tail;
  g_young_root_q = header._nextUntrackable;
  postcond(g_young_root_q != NULL);

  rtgc_log(LOG_OPT(6), "young roots %d -> garbage = %d, young = %d\n", cnt_young_root, cnt_garbage, young_root);
  debug_only(cnt_young_root = young_root);
}



struct TraceInfo {
  void* move_to;
  oopDesc* marked;
};

static void register_referrer(oopDesc* ref, TraceInfo* ti) {
  GCObject* obj = to_obj(ref);
  if (!obj->isTrackable() && !obj->hasReferrer()) {
	  debug_only(cnt_young_root++;)
    rtgc_log(LOG_OPT(8), "add young root %p root=%p\n", ref, g_young_root_q);
    obj->_nextUntrackable = g_young_root_q;
    g_young_root_q = obj;
  }
  RTGC::add_referrer_unsafe(ref, ti->marked, ti->move_to, "regr");
  postcond(obj->hasReferrer());
}

void RTGC::unmark_trackable(oopDesc* ptr) {
  GCObject* obj = to_obj(ptr);
  if (obj->isTrackable()) {
    obj->removeAllReferrer();
  }
}

void RTGC::mark_empty_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  obj->markTrackable();
}

void RTGC::mark_pending_trackable(oopDesc* marked, void* move_to) {
  if (!REF_LINK_ENABLED) return;
  rtgc_log(LOG_OPT(5), "mark_pending_trackable %p (move to -> %p)\n", marked, move_to);
  precond((void*)marked->forwardee() == move_to);
  GCObject* obj = to_obj(marked);
  obj->markTrackable();
  g_promted_trackables.append((oopDesc*)move_to);
}

void RTGC::mark_promoted_trackable(oopDesc* old_p, oopDesc* new_p) {
  if (!REF_LINK_ENABLED) return;
  // 이미 객체가 복사된 상태이므로, 둘 다 marking 되어야 한다.
  // old_p 를 marking 하여, young_roots 에서 제거될 수 있도록 하고,
  // new_p 를 marking 하여, young_roots 에 등록되지 않도록 한다.
  to_obj(old_p)->markTrackable();
  to_obj(new_p)->markTrackable();
  rtgc_log(LOG_OPT(6), "mark_promoted_trackable %p(moved to %p)\n", old_p, new_p);
  g_promted_trackables.append(new_p);
}

void RTGC::flush_trackables() {
  if (!REF_LINK_ENABLED) return;

  const int count = g_promted_trackables.length();
  if (count == 0) return;

  rtgc_log(LOG_OPT(6), "flush_trackables %d\n", count);
  oopDesc** pOop = &g_promted_trackables.at(0);
  for (int i = count; --i >= 0; ) {
    oopDesc* ptr = *pOop++;
    TraceInfo ti;
    ti.move_to = ptr->forwardee();
    ti.marked = ptr;
    RTGC::iterateReferents(to_obj(ptr), (RefTracer2)register_referrer, &ti);
  }
  g_promted_trackables.trunc_to(0);
}

