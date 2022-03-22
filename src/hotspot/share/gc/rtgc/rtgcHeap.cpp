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
  class RtAdjustPointerClosure: public BasicOopIterateClosure {
  public:
    template <typename T> void do_oop_work(T* p);
    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
    virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }

    void init_old_gen_start() {
      _old_gen_start = GenCollectedHeap::heap()->old_gen()->reserved().start(); 
      _old_anchor_p = NULL;
    }
    bool is_in_young(void* p) { return p < _old_gen_start; }
    bool has_young_ref() { return _has_young_ref; }
    void set_has_young_ref(bool has_young_ref) { _has_young_ref = has_young_ref; }
    void set_old_anchor_p(oopDesc* old_anchor_p, oopDesc* new_anchor_p, bool is_reference) { 
      _old_anchor_p = old_anchor_p; 
      _new_anchor_p = new_anchor_p; 
      _is_java_reference = is_reference;
      _has_young_ref = false; 
    }
  private:
    bool _has_young_ref;
    HeapWord* _old_gen_start;
    oopDesc* _old_anchor_p;
    oopDesc* _new_anchor_p;
    bool _is_java_reference;
  };


  class ResurrectionClosure : public BasicOopIterateClosure {
  public:  
    OopClosure* _survivor_closure;
    ResurrectionClosure(OopClosure* survivor_closure) {
      _survivor_closure = survivor_closure;
    }

    template <typename T> void do_oop_work(T* p) {
      oop obj = RawAccess<>::oop_load(p);
      if (!CompressedOops::is_null(obj)) {
        if (!to_obj(obj)->isTrackable()) {
          rtgc_log(true, "found lost %p(%s)\n",
              (void*)obj, RTGC::getClassName(to_obj(obj)));
          _survivor_closure->do_oop(p);
        }
        else if (to_obj(obj)->isGarbageMarked()) {
          rtgc_log(true, "resurrect garabage %p(%s)\n",
              (void*)obj, RTGC::getClassName(to_obj(obj)));
          to_obj(obj)->unmarkGarbage();
          obj->oop_iterate(this);
        }
      }
    }
    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
  };


  class StackRefCountClosure : public OopClosure {
    template <typename T> void do_oop_work(T* p) {
      oop obj = RawAccess<>::oop_load(p);
      if (!CompressedOops::is_null(obj)) {
        if (to_obj(obj)->isTrackable()) {
          rtHeap::mark_stack_root(obj);
        }
      }
    }

    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
  };

  StackRefCountClosure g_stack_ref_count_closure;
  ResurrectionClosure g_resurrection_closure(&g_stack_ref_count_closure);
  GrowableArrayCHeap<oop, mtGC> g_pending_trackables;
  GrowableArrayCHeap<oop, mtGC> g_young_roots;
  GrowableArrayCHeap<GCNode*, mtGC> g_stack_roots;
  SimpleVector<GCObject*> g_garbage_list;
  Thread* gcThread = NULL;
  int g_cntTrackable = 0;
  int g_saved_young_root_count = -1;
  RtAdjustPointerClosure g_adjust_pointer_closure;
  const bool USE_PENDING_TRACKABLES = false;
};
using namespace RTGC;


static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

static bool IS_GC_MARKED(oopDesc* obj) {
  return obj->is_gc_marked();
}

static bool is_java_reference(oopDesc* obj) {
  return (obj->klass()->id() == InstanceRefKlassID);
}

static bool is_java_reference_with_young_referent(oopDesc* obj) {
  if (!is_java_reference(obj)) return false;

  void* referent_addr = java_lang_ref_Reference::referent_addr_raw(obj);
  oop referent;
  if (RTGC::is_narrow_oop_mode) {
    referent = RawAccess<>::oop_load((narrowOop*)referent_addr);
  }
  else {
    referent = RawAccess<>::oop_load((oop*)referent_addr);
  }
  return referent != NULL && !to_obj(referent)->isTrackable();
}






void rtHeap::adjust_tracking_pointers(oopDesc* old_p, bool is_young_root) {
  precond(old_p->is_gc_marked() || 
      (old_p->forwardee() == NULL && !RTGC_REMOVE_GARBAGE_REFERRER_ON_ADJUST_POIINTER));

  GCObject* obj = to_obj(old_p);
  if (!IS_GC_MARKED(old_p)) {
    precond(!obj->hasReferrer());
    precond(!is_young_root);
    return;
  }

  if (is_young_root) {
    precond(obj->isTrackable());
    if (!obj->isYoungRoot()) {
      obj->markYoungRoot();
      oopDesc* forwardee = old_p->forwardee();
      if (forwardee == NULL) forwardee = old_p;
      rtgc_log(false && RTGC::debugOptions[0], "mark YG Root %p\n", forwardee);
      g_young_roots.append(forwardee);
    } else {
      postcond(is_java_reference(old_p));
    }
  }

  if (!obj->hasReferrer()) {
    return;
  }

  void* moved_to = old_p->mark().decode_pointer();
  const bool CHECK_GARBAGE = RTGC_REMOVE_GARBAGE_REFERRER_ON_ADJUST_POIINTER;

  if (obj->hasMultiRef()) {
    ReferrerList* referrers = obj->getReferrerList();
    for (int idx = 0; idx < referrers->size(); ) {
      oopDesc* old_p = cast_to_oop(referrers->at(idx));
      if (CHECK_GARBAGE && !IS_GC_MARKED(old_p)) {
        rtgc_log(LOG_OPT(11), "remove garbage ref %p in %p(move to->%p)\n", old_p, obj, moved_to);
        referrers->removeFast(idx);
        continue;
      }
      GCObject* new_obj = (GCObject*)old_p->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(11), "ref moved %p->%p in %p\n", 
          old_p, new_obj, obj);
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
    oopDesc* old_p = cast_to_oop(_offset2Object(obj->_refs, &obj->_refs));
    if (CHECK_GARBAGE && !IS_GC_MARKED(old_p)) {
      rtgc_log(LOG_OPT(11), "remove garbage ref %p in %p(move to->%p)\n", old_p, obj, moved_to);
      obj->_refs = 0;
    }
    else {
      GCObject* new_obj = (GCObject*)old_p->mark().decode_pointer();
      if (new_obj != NULL && new_obj != (void*)0xbaadbabebaadbabc) {
        rtgc_log(LOG_OPT(11), "ref moved %p->%p in %p\n", 
          old_p, new_obj, obj);
        obj->_refs = _pointer2offset(new_obj, &obj->_refs);
      }
    }
  }
}

#ifdef ASSERT
void RTGC::mark_dead_space(void* p) {
  ((GCNode*)p)->markGarbage();
}
bool RTGC::is_young_root(void* p) {
  return ((GCNode*)p)->isYoungRoot();
}

#endif

void rtHeap::refresh_young_roots() {
  /**
   * @brief 
   * full-gc : 객체를 이동할 주소를 모두 설정한 후 호출된다.
   * 객체 이동 전에 리스트에서 가비지 객체를 제거하여야 한다. 
   * yg-gc : 현재 사용하지 않음.
   */


  debug_only(if (gcThread == NULL) gcThread = Thread::current();)
  precond(gcThread == Thread::current());
  g_adjust_pointer_closure.init_old_gen_start();

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
    if (IS_GC_MARKED(p)) {
      oopDesc* p2 = p->forwardee();
      postcond(p2 != (void*)0xbaadbabebaadbabc);
      if (p2 == NULL) p2 = p;
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
  rtgc_log(false && RTGC::debugOptions[0], "mark YG Root %p idx=%d\n", obj, g_young_roots.length());
}

bool rtHeap::is_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isTrackable();
}

void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  if (obj->isTrackable()) {
    obj->removeAllReferrer();
    debug_only(g_cntTrackable --);
  }
  obj->markGarbage();
}

static oopDesc* empty_trackable;
void rtHeap::mark_empty_trackable(oopDesc* p) {
  if (CHECK_EMPTY_TRACKBLE) {
    bool is_dead_space = SafepointSynchronize::is_at_safepoint()
        && Thread::current()->is_VM_thread();
    if (!is_dead_space) {
      rtgc_log(true, "mark_empty_trackable. It must be found in promoted trackable !!! %p\n", p);
      empty_trackable = p;
    }
    else {
      debug_only(to_obj(p)->isGarbageMarked();)
    }
    /** 주로 dead-space 가 등록된다. 크기가 큰 array 나, young-space 가 부족한 경우 */
    // rtgc_log(LOG_OPT(9), "mark_empty_trackable %p\n", p);
    // GCObject* obj = to_obj(p);
    // obj->markTrackable();
    // debug_only(g_cntTrackable++);
  }
}

void rtHeap::mark_pending_trackable(oopDesc* old_p, void* new_p) {
  /**
   * @brief Full-GC 과정에서 adjust_pointers 를 수행하기 직전에 호출된다.
   * 즉, old_p 객체의 field는 유효한 old 객체를 가리키고 있다.
   */
  //if (!REF_LINK_ENABLED) return;
  if (CHECK_EMPTY_TRACKBLE) {
    empty_trackable = NULL;
    // assert(new_p != RTGC::debug_obj, "empty_trackable catched!");
  }
  rtgc_log(LOG_OPT(9), "mark_pending_trackable %p (move to -> %p)\n", old_p, new_p);
  precond((void*)old_p->forwardee() == new_p || (old_p->forwardee() == NULL && old_p == new_p));
  to_obj(old_p)->markTrackable();
  debug_only(g_cntTrackable++);
  if (USE_PENDING_TRACKABLES) {
    g_pending_trackables.append((oopDesc*)new_p);
  }
  if (is_java_reference_with_young_referent(old_p)) {
    /* adjust_pointers 수행 전에 referent 검사하여야 한다.
       또는 객체 복사가 모두 종료된 시점에 referent를 검사할 수 있다.
    */
    to_obj(old_p)->markYoungRoot();
    oopDesc* forwardee = old_p->forwardee();
    if (forwardee == NULL) forwardee = old_p;
    rtgc_log(false && RTGC::debugOptions[0], "mark YG Root %p\n", new_p);
    g_young_roots.append((oopDesc*)new_p);
  }

}

void rtHeap::mark_promoted_trackable(oopDesc* new_p) {
  /**
   * @brief YG GC 수행 중, old-g로 옮겨진 객체들에 대하여 호출된다.
   */
  if (CHECK_EMPTY_TRACKBLE && new_p == empty_trackable) {
    empty_trackable = NULL;
    // rtgc_log(true, "empty_trackable catched!\n");
  }
  // 이미 객체가 복사된 상태이다.
  // old_p 를 marking 하여, young_roots 에서 제거될 수 있도록 하고,
  // new_p 를 marking 하여, young_roots 에 등록되지 않도록 한다.
  rtgc_log(LOG_OPT(11), "mark_promoted_trackable %p, tr=%d\n", new_p, to_obj(new_p)->isTrackable());
  if (to_obj(new_p)->isTrackable()) return;
  to_obj(new_p)->markTrackable();
  debug_only(g_cntTrackable++);

}

static void add_referrer_and_check_young_root(oopDesc* p, oopDesc* base) {
  if (USE_PENDING_TRACKABLES) {
    if (!to_obj(p)->isTrackable() && !to_obj(base)->isYoungRoot()) {
      RTGC::add_young_root(base);
    }
  }
  RTGC::add_referrer_unsafe(p, base);
}

static void dump_anchors(GCObject* obj) {
    AnchorIterator it(obj);
    while (it.hasNext()) {
      GCObject* R = it.next();
      rtgc_log(true, "anchor %p(%s)\n", R, RTGC::getClassName(R));
    }  
}

bool rtHeap::flush_pending_trackables() {
  if (CHECK_EMPTY_TRACKBLE) {
    assert(empty_trackable == NULL, "empty_trackable is not catched!");
  }

  if (RTGC::debugOptions[0]) {
    GCRuntime::adjustShortcutPoints();
  }

  g_saved_young_root_count = -1;
  g_garbage_list.resize(0);
  g_resurrection_closure._survivor_closure = &g_stack_ref_count_closure;

  if (!USE_PENDING_TRACKABLES) return false;
  const int count = g_pending_trackables.length();
  rtgc_log(LOG_OPT(11), "flush_pending_trackables %d\n", count);
  if (count == 0) return false;

  oop* pOop = &g_pending_trackables.at(0);
  for (int i = count; --i >= 0; ) {
    oopDesc* p = *pOop++;
    RTGC::iterateReferents(to_obj(p), (RefTracer2)RTGC::add_referrer_unsafe, p);
  }
  g_pending_trackables.trunc_to(0);
  return true;
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
      rtgc_log(LOG_OPT(11), "iterate young_ref %p->%p\n", 
          (void*)ref, (void*)CompressedOops::decode_not_null(*p));
      if (!to_obj(ref)->isTrackable()) {
        cnt_young_ref ++;
      }
    }
  }

  virtual void do_oop(narrowOop* p) { do_oop_work(p); }
  virtual void do_oop(oop*       p) { do_oop_work(p); }
};

void rtHeap::mark_stack_root(oopDesc* new_p) {
  // fatal("mark_stack_root");
  GCNode* node = to_node(new_p);
  if (node->getRootRefCount() > 0) return;
  rtgc_log(LOG_OPT(9), "add stack root %p\n", new_p);
  node->incrementRootRefCount();
  g_stack_roots.append(node);
}

void rtHeap::switch_young_roots() {
  g_saved_young_root_count = g_young_roots.length();
}

void rtHeap__clear_garbage_young_roots() {
  int idx_root;
  if ((idx_root = g_saved_young_root_count) > 0) {
    // OldAnchorAdustPointer ap(closure);
    oop* src = g_young_roots.adr_at(0);
    oop* dst = src;
    for (;--idx_root >= 0; src++) {
      GCObject* anchor = to_obj(*src);
      if (anchor->isUnsafe() &&
          GarbageProcessor::detectUnreachable(anchor, g_garbage_list)) {
        rtgc_log(true, "garbage YG Root %p(%s)\n", (void*)anchor, RTGC::getClassName(to_obj(anchor)));
        continue;      
      }  
    }
  }
}

void rtHeap::iterate_young_roots(BoolObjectClosure* closure, OopClosure* survivor_closure) {
  /**
   * 참고) promoted object 에 대한 adjust_pointer 실행 전 상태.
   */
  rtgc_log(LOG_OPT(8), "iterate_young_roots %d/%d stack %d\n", 
      g_saved_young_root_count, g_young_roots.length(), g_stack_roots.length());

  g_resurrection_closure._survivor_closure = survivor_closure;

  int idx_root;
  if ((idx_root = g_saved_young_root_count) > 0) {
    // OldAnchorAdustPointer ap(closure);
    oop* src = g_young_roots.adr_at(0);
    oop* dst = src;
    for (;--idx_root >= 0; src++) {
      oopDesc* anchor = *src;
      if (RTGC::debugOptions[0] && 
          to_obj(anchor)->isUnsafe() &&
          GarbageProcessor::detectUnreachable(to_obj(anchor), g_garbage_list)) {
        rtgc_log(true, "garbage YG Root %p(%s)\n", (void*)anchor, RTGC::getClassName(to_obj(anchor)));
        RTGC::print_anchor_list(anchor);
        continue;      
      }
      
      rtgc_log(LOG_OPT(11), "iterate anchor %p\n", (void*)anchor);
      bool is_root = closure->do_object_b(anchor)
                  || is_java_reference_with_young_referent(anchor);
      if (!is_root) {
        to_obj(anchor)->unmarkYoungRoot();
      } else {
        if (dst != src) {
          *dst = anchor;
        }
        dst ++;
      }
    }
    int remain_roots = dst - &g_young_roots.at(0);
    int new_roots = g_young_roots.length() - g_saved_young_root_count;
    if (new_roots > 0) {
      int space = g_saved_young_root_count - remain_roots;
      if (space >= new_roots) {
        memcpy(dst, g_young_roots.adr_at(g_saved_young_root_count), new_roots * sizeof(void*));
      }
      else if (space > 0) {
        memcpy(dst, g_young_roots.adr_at(g_young_roots.length() - space), space * sizeof(void*));
      }
      remain_roots += new_roots;
    }

    rtgc_log(LOG_OPT(8), "iterate_young_roots done %d->%d garbage=%d\n", 
        g_young_roots.length(), remain_roots, g_garbage_list.size());
    g_young_roots.trunc_to(remain_roots);
  }

  if ((idx_root = g_stack_roots.length()) > 0) {
    GCNode** src = &g_stack_roots.at(0);
    for (;--idx_root >= 0; src++) {
      src[0]->decrementRootRefCount();
    }
    rtgc_log(LOG_OPT(8), "iterate_stack_roots done %d\n", 
        g_stack_roots.length());
    g_stack_roots.trunc_to(0);
  }
}


void rtHeap::mark_reachable_from_YG(oopDesc* tenured_p) {
  if (g_saved_young_root_count < 0) {
    mark_stack_root(tenured_p);
    return;
  }
  GCObject* obj = to_obj(tenured_p);
  if (!obj->isGarbageMarked()) return;
  rtgc_log(true, "resurrect garabage %p(%s)\n",
      (void*)obj, RTGC::getClassName(obj));
  obj->unmarkGarbage();
  tenured_p->oop_iterate(&g_resurrection_closure);
}

void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.length(), full_gc); 
}

void rtHeap::add_trackable_link(oopDesc* anchor, oopDesc* link, bool is_young_root) {
  rtgc_log(false && is_young_root != !to_obj(link)->isTrackable(), 
      "add_trackable_link %p(%d) -> anchor=%p(%d)\n", 
      link, to_obj(link)->isTrackable(), anchor, to_obj(anchor)->isTrackable()); 
  assert(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)\n", anchor, anchor->klass()->name()->bytes());
  if (to_obj(link)->isGarbageMarked()) {
    rtHeap::mark_reachable_from_YG(link);
  }
  if (is_young_root && !to_obj(anchor)->isYoungRoot()) {
    RTGC::add_young_root(anchor);
  }
  RTGC::add_referrer_unsafe(link, anchor);
}

bool rtHeap::is_alive(oopDesc* p) {
  return !to_obj(p)->isGarbageMarked();
}




template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  if (old_p != NULL && _new_anchor_p != NULL) {
    if (is_in_young(new_p)) {
      set_has_young_ref(true);
    }
    if (USE_PENDING_TRACKABLES) return;

    if (_is_java_reference) {
      ptrdiff_t offset = (address)p - (address)_old_anchor_p;
      if (offset == java_lang_ref_Reference::discovered_offset()
      ||  offset == java_lang_ref_Reference::referent_offset()) {
        return;
      }
    }
    // _old_anchor_p 는 old-address를 가지고 있으므로, Young root로 등록할 수 없다.
    if (to_obj(old_p)->isDirtyReferrerPoints()) {
      // old_p 에 대해 이미 adjust_pointers 를 수행하기 전.
      RTGC::add_referrer_unsafe(old_p, _old_anchor_p);
    }
    else {
      // old_p 에 대해 이미 adjust_pointers 가 수행됨.
      RTGC::add_referrer_unsafe(old_p, _new_anchor_p);
    }
  }
}

void rtHeap::mark_forwarded(oopDesc* p) {
  if (!USE_PENDING_TRACKABLES) {
    to_obj(p)->markDirtyReferrerPoints();
  }
}

size_t rtHeap::adjust_pointers(oopDesc* old_p) {
  oopDesc* new_anchor_p = NULL;
  bool is_reference = false;
  if (!to_obj(old_p)->isTrackable()) {
    oopDesc* p = old_p->forwardee();
    if (p == NULL) p = old_p;
    if (!g_adjust_pointer_closure.is_in_young(p)) {
      mark_pending_trackable(old_p, p);
      is_reference = is_java_reference(old_p);
      new_anchor_p = p;
    }
  }
  g_adjust_pointer_closure.set_old_anchor_p(old_p, new_anchor_p, is_reference);
  /**
   * @brief oop_iterate, oop_iterate_size 는 Reference.referent 와 discovered 도
   * Scan 한다. 이 때, Reference 를 referent 의 anchor 로 추가하지 않아야 한다.
   */
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool has_young_ref = g_adjust_pointer_closure.has_young_ref();
  adjust_tracking_pointers(old_p, has_young_ref); 
  if (!USE_PENDING_TRACKABLES) {
    to_obj(old_p)->unmarkDirtyReferrerPoints();
  }
  return size; 
}
