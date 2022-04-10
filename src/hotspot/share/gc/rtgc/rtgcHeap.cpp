#include "precompiled.hpp"

#include "runtime/globals.hpp"
#include "utilities/debug.hpp"
#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "gc/shared/genCollectedHeap.hpp"
#include "gc/serial/markSweep.inline.hpp"
#include "gc/shared/referenceDiscoverer.hpp"
#include "oops/instanceRefKlass.inline.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

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
    void init(oopDesc* old_anchor_p, oopDesc* new_anchor_p, bool is_java_reference) { 
      _old_anchor_p = old_anchor_p; 
      _new_anchor_p = new_anchor_p; 
#if RTGC_IGNORE_JREF
      _is_java_reference = is_java_reference;
#endif      
      _has_young_ref = false; 
    }
    bool _has_young_ref;
  private:
    HeapWord* _old_gen_start;
    oopDesc* _old_anchor_p;
    oopDesc* _new_anchor_p;
#if RTGC_IGNORE_JREF
    bool _is_java_reference;
#endif    
  };

  class RtAnchorRemoveClosure: public BasicOopIterateClosure {
  public:
    RtAnchorRemoveClosure(GCObject* garbage) : _garbage_anchor(garbage) {}
    template <typename T> void do_oop_work(T* p);
    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
  private:
    GCObject* _garbage_anchor;
  };




  GrowableArrayCHeap<oop, mtGC> g_pending_trackables;
  GrowableArrayCHeap<oop, mtGC> g_young_roots;
  GrowableArrayCHeap<oop, mtGC> g_keep_alives;
  GrowableArrayCHeap<GCNode*, mtGC> g_stack_roots;
  SimpleVector<GCObject*> g_garbage_list;
  Thread* gcThread = NULL;
  int g_cntTrackable = 0;
  int g_saved_young_root_count = 0;
  oopDesc* g_phantom_ref = NULL;
  RtAdjustPointerClosure g_adjust_pointer_closure;
  BoolObjectClosure* g_young_root_closure;
  const bool USE_PENDING_TRACKABLES = false;
  oopDesc* empty_trackable;
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
  oop referent = java_lang_ref_Reference::unknown_referent_no_keepalive(obj);
  return referent != NULL && !to_obj(referent)->isTrackable();
}


// static void dump_anchor_tree(int depth, GCObject* node) {
//   // for (int i = depth; --i >= 0; ) {
//   //   printf("- ");
//   // }
//   printf("[%d] %p(%s:%d ygR:%d):%d anchors:%d\n", 
//     depth, node, RTGC::getClassName(node), node->isGarbageMarked(), 
//     node->isYoungRoot(), node->getRootRefCount(), node->hasReferrer());
//   if (node->getRootRefCount() > 0) return;
//   node->incrementRootRefCount();

//   AnchorIterator it(node);
//   while (it.hasNext()) {
//     GCObject* anchor = it.next();
//     dump_anchor_tree(depth + 1, anchor);
//   }
// }


bool rtHeap::is_alive(oopDesc* p) {
  return !to_obj(p)->isGarbageMarked();
}






#ifdef ASSERT
void RTGC::mark_dead_space(void* p) {
  ((GCNode*)p)->markGarbage();
}
bool RTGC::is_young_root(void* p) {
  return ((GCNode*)p)->isYoungRoot();
}

#endif

void rtHeap::add_young_root(oopDesc* old_p, oopDesc* new_p) {
  precond(to_obj(old_p)->isTrackable());
  precond(!to_obj(old_p)->isGarbageMarked());
  to_obj(old_p)->markYoungRoot();
  g_young_roots.append(new_p);
  rtgc_log(LOG_OPT(11), "mark YG Root (%p)->%p idx=%d\n", old_p, new_p, g_young_roots.length());
}

bool rtHeap::is_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isTrackable();
}

#if RTGC_CHECK_EMPTY_TRACKBLE
void rtHeap::mark_empty_trackable(oopDesc* p) {
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
#endif


/**
 * @brief YG GC 수행 중, old-g로 옮겨진 객체들에 대하여 호출된다.
 */
static int cntDD = 0;
void rtHeap::mark_promoted_trackable(oopDesc* new_p) {
  if (RTGC_CHECK_EMPTY_TRACKBLE && new_p == empty_trackable) {
    empty_trackable = NULL;
  }
  // 이미 객체가 복사된 상태이다.
  // old_p 를 marking 하여, young_roots 에서 제거될 수 있도록 하고,
  // new_p 를 marking 하여, young_roots 에 등록되지 않도록 한다.
  rtgc_log(LOG_OPT(11), "mark_promoted_trackable %p, tr=%d\n", new_p, to_obj(new_p)->isTrackable());
  if (to_obj(new_p)->isTrackable()) return;
  to_obj(new_p)->markTrackable();
  debug_only(g_cntTrackable++);
}

static void resurrect_young_root(GCObject* node) {
  precond(node->isYoungRoot());
  precond(node->isGarbageMarked());
  rtgc_log(LOG_OPT(11), "young_root(%p) resurrected\n", node);
  node->unmarkGarbage();
  if (!g_young_root_closure->do_object_b(cast_to_oop(node))) {
    node->unmarkYoungRoot();
  }
}


void rtHeap::mark_survivor_reachable(oopDesc* new_p, bool unused) {
  GCObject* node = to_obj(new_p);
  precond(node->isTrackable());
  if (node->isGarbageMarked()) {
    resurrect_young_root(node);
  }

  if (node->getRootRefCount() > 0) return;
  rtgc_log(LOG_OPT(9), "add stack root %p\n", new_p);
  GCRuntime::onAssignRootVariable_internal(node);
  g_stack_roots.append(node);
}

/** Reference 가 old-G로 이동한 경우, full-GC 전까지 해당 referent를 reachable 상태로 유지한다. */
void rtHeap::mark_keep_alive(oopDesc* referent) {
  GCObject* node = to_obj(referent);
  if (!node->isKeepAlive()) {
    if (node->isGarbageMarked()) {
      resurrect_young_root(node);
    }
    rtgc_log(node == RTGC::debug_obj, "mark_keep_alive %p\n", referent);
    node->markKeepAlive();
    postcond(node->isKeepAlive() && node->getRootRefCount() > 0);
    g_keep_alives.append(referent);
  }
}

#ifdef ASSERT
static bool check_garbage(GCObject* node, bool checkByGarbageMark) {
  if (!node->isAnchored()) return true;
  if (node->getRootRefCount() > 0) return false;
  AnchorIterator ai(node);
  while (ai.hasNext()) {
    GCObject* anchor = ai.next();
    if (checkByGarbageMark) {
      if (!anchor->isGarbageMarked()) {
        rtgc_log(true, "invalid anchor yg-root %p, yg-r=%d, rc=%d:%d\n",
              anchor, anchor->isYoungRoot(), anchor->getRootRefCount(), anchor->hasReferrer());
        return false;
      }
    }
    else if (IS_GC_MARKED(cast_to_oop(anchor)) && cast_to_oop(anchor)->klass()->id() != InstanceRefKlassID) {
        rtgc_log(true, "invalid anchor yg-root %p, yg-r=%d, rc=%d:%d\n",
              anchor, anchor->isYoungRoot(), anchor->getRootRefCount(), anchor->hasReferrer());
        return false;
    }
  }
  return true;
}
#endif

void rtHeap__clear_garbage_young_roots() {
  if (g_saved_young_root_count > 0) {
    oop* src = g_young_roots.adr_at(0);
    oop* end = src + g_saved_young_root_count;
    for (; src < end; src++) {
      GCObject* node = to_obj(*src);
      if (node->isGarbageMarked()) {
        assert(check_garbage(node, true), "invalid yg-root %p, yg-r=%d, rc=%d:%d\n",
              node, node->isYoungRoot(), node->getRootRefCount(), node->hasReferrer());
      }
      else if (node->isUnsafe() && 
          GarbageProcessor::detectUnreachable(node, g_garbage_list)) {
          rtgc_log(LOG_OPT(11), "garbage YG Root %p(%s)\n", (void*)node, RTGC::getClassName(node));
      }  
    }

    src = g_young_roots.adr_at(0);
    oop* dst = src;
    for (; src < end; src++) {
      GCObject* node = to_obj(*src);
      if (node->isYoungRoot() && !node->isGarbageMarked()) {
        if (dst != src) {
          *dst = cast_to_oop(node);
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

    rtgc_log(LOG_OPT(8), "rtHeap__clear_garbage_young_roots done %d->%d garbage=%d\n", 
        g_young_roots.length(), remain_roots, g_garbage_list.size());
    g_young_roots.trunc_to(remain_roots);
    g_saved_young_root_count = 0;
  }

  int idx_root;
#ifdef ASSERT
  if ((idx_root = g_young_roots.length()) > 0) {
    oop* src = g_young_roots.adr_at(0);
    oop* end = src + idx_root;
    for (; src < end; src++) {
      GCObject* node = to_obj(*src);
      precond(!node->isGarbageMarked());
    }
  }
#endif

  if ((idx_root = g_stack_roots.length()) > 0) {
    GCNode** src = &g_stack_roots.at(0);
    GCNode** end = src + idx_root;
    for (; src < end; src++) {
      src[0]->decrementRootRefCount();
    }
    rtgc_log(LOG_OPT(8), "iterate_stack_roots done %d\n", 
        g_stack_roots.length());
    g_stack_roots.trunc_to(0);
  }
}

void rtHeap::iterate_young_roots(BoolObjectClosure* closure, OopClosure* unused) {
  g_young_root_closure = closure;
  g_saved_young_root_count = g_young_roots.length();
  rtgc_log(LOG_OPT(8), "iterate_young_roots %d stack %d\n", 
      g_saved_young_root_count, g_stack_roots.length());

  if (g_saved_young_root_count == 0) return;

  oop* src = g_young_roots.adr_at(0);
  oop* end = src + g_young_roots.length();
  for (;src < end; src++) {
    GCObject* node = to_obj(*src);
    precond(!node->isGarbageMarked());
    if (!node->isAnchored()) {
      node->markGarbage();
      rtgc_log(true || LOG_OPT(11), "skip garbage node %p\n", (void*)node);
      continue;
    }

    rtgc_log(LOG_OPT(11), "iterate yg root %p\n", (void*)node);
    // referent 자동 검사됨.
    bool is_root = closure->do_object_b(cast_to_oop(node));
    if (!is_root) {
      node->unmarkYoungRoot();
    } else {
      // postcond(!is_java_reference_with_young_referent(cast_to_oop(node))
      //   || !GenCollectedHeap::is_in_young(cast_to_oop(node)));
    }
  }
}


void rtHeap::add_promoted_link(oopDesc* anchor, oopDesc* link, bool young_ref_reahcable) {
  assert(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)\n", anchor, anchor->klass()->name()->bytes());
  if (young_ref_reahcable) {
    rtHeap::mark_survivor_reachable(link);
  } else {
    precond(!to_obj(link)->isGarbageMarked());
  }

  RTGC::add_referrer_unsafe(link, anchor);
}


template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  if (old_p == NULL) return;

  _has_young_ref |= is_in_young(new_p);
  if (_new_anchor_p == NULL) return;

  if (USE_PENDING_TRACKABLES) return;
#if RTGC_IGNORE_JREF
  if (_is_java_reference) {
    ptrdiff_t offset = (address)p - (address)_old_anchor_p;
    if (offset == java_lang_ref_Reference::discovered_offset()
    ||  offset == java_lang_ref_Reference::referent_offset()) {
      return;
    }
  }
#endif
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

void rtHeap::mark_forwarded(oopDesc* p) {
  precond(!to_node(p)->isGarbageMarked());
  if (!USE_PENDING_TRACKABLES) {
    to_obj(p)->markDirtyReferrerPoints();
  }
}

void rtHeap::mark_pending_trackable(oopDesc* old_p, void* new_p) {
  /**
   * @brief Full-GC 과정에서 adjust_pointers 를 수행하기 직전에 호출된다.
   * 즉, old_p 객체의 field는 유효한 old 객체를 가리키고 있다.
   */
  if (RTGC_CHECK_EMPTY_TRACKBLE) {
    empty_trackable = NULL;
  }
  rtgc_log(LOG_OPT(9), "mark_pending_trackable %p (move to -> %p)\n", old_p, new_p);
  precond((void*)old_p->forwardee() == new_p || (old_p->forwardee() == NULL && old_p == new_p));
  to_obj(old_p)->markTrackable();
  debug_only(g_cntTrackable++);
  if (USE_PENDING_TRACKABLES) {
    g_pending_trackables.append((oopDesc*)new_p);
  }
  // if (is_java_reference_with_young_referent(old_p)) {
  //   /* adjust_pointers 수행 전에 referent 검사하여야 한다.
  //      또는 객체 복사가 모두 종료된 시점에 referent를 검사할 수 있다.
  //   */
  //   add_young_root(old_p, (oopDesc*)new_p);
  // }
}

static int cntGG = 0;
size_t rtHeap::adjust_pointers(oopDesc* old_p) {
  oopDesc* new_anchor_p = NULL;
  bool is_java_reference = false;
  int rc = to_obj(old_p)->getRootRefCount();
  if (!to_obj(old_p)->isTrackable()) {
    oopDesc* p = old_p->forwardee();
    if (p == NULL) p = old_p;
    if (!g_adjust_pointer_closure.is_in_young(p)) {
      mark_pending_trackable(old_p, p);
      new_anchor_p = p;
#if RTGC_IGNORE_JREF
      is_java_reference = old_p->klass()->id() == InstanceRefKlassID;
#endif      
    }
  }
  rtgc_log(g_adjust_pointer_closure.is_in_young(old_p) && 
          to_obj(old_p)->getRootRefCount() > 0 && old_p->klass() == vmClasses::String_klass(),
      "adjust_pointers %p(%d:%d) %p(%d)\n", old_p, g_adjust_pointer_closure.is_in_young(old_p), rc,
        RTGC::getForwardee(to_obj(old_p)), ++cntGG); 
  g_adjust_pointer_closure.init(old_p, new_anchor_p, is_java_reference);
  /**
   * @brief oop_iterate, oop_iterate_size 는 Reference.referent 와 discovered 도
   * Scan 한다. 이 때, Reference 를 referent 의 anchor 로 추가하지 않아야 한다.
   */
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);
  bool is_young_root = g_adjust_pointer_closure._has_young_ref && to_obj(old_p)->isTrackable();
  adjust_anchor_pointers(old_p, is_young_root); 
  if (!USE_PENDING_TRACKABLES) {
    to_obj(old_p)->unmarkDirtyReferrerPoints();
  }
  return size; 
}

bool adjust_anchor_pointer(ShortOOP* p, GCObject* node) {
  oopDesc* old_p = cast_to_oop((void*)p[0]);
  if (!IS_GC_MARKED(old_p)) {
    rtgc_log(LOG_OPT(11), "garbage anchor %p in %p\n", old_p, node);
    return false;
  }

  GCObject* new_obj = (GCObject*)old_p->mark().decode_pointer();
  if (new_obj != NULL) {
    precond(new_obj != (void*)0xbaadbabebaadbabc);
    rtgc_log(LOG_OPT(11), "anchor moved %p->%p in %p\n", 
      old_p, new_obj, node);
    p[0] = new_obj;
  }  
  return true;
}

void rtHeap::adjust_anchor_pointers(oopDesc* old_p, bool is_young_root) {
  precond(old_p->is_gc_marked() || 
      (old_p->forwardee() == NULL && !RTGC_REMOVE_GARBAGE_REFERRER_ON_ADJUST_POINTER));

  GCObject* obj = to_obj(old_p);
  if (!IS_GC_MARKED(old_p)) {
    precond(!obj->hasReferrer());
    precond(!is_young_root);
    return;
  }

  if (is_young_root) {
    precond(obj->isTrackable());
    if (!obj->isYoungRoot()) {
      oopDesc* forwardee = old_p->forwardee();
      if (forwardee == NULL) forwardee = old_p;
      add_young_root(old_p, forwardee);
    } else {
      //postcond(is_java_reference(old_p));
    }
  }

  if (!obj->hasReferrer()) {
    return;
  }

  const bool CHECK_GARBAGE = RTGC_REMOVE_GARBAGE_REFERRER_ON_ADJUST_POINTER;
  bool check_shortcut = false;

  if (obj->hasMultiRef()) {
    ReferrerList* referrers = obj->getReferrerList();
    ShortOOP* ppAnchor = referrers->adr_at(0);
    int cntAnchor = referrers->size();
    check_shortcut = !cast_to_oop((void*)ppAnchor[0])->is_gc_marked();
    for (int idx = 0; idx < cntAnchor; ) {
      if (adjust_anchor_pointer(ppAnchor, obj) || !CHECK_GARBAGE) {
        ppAnchor++; idx++;
      } else {
        ppAnchor[0] = referrers->at(--cntAnchor);
      }
    }

    if (CHECK_GARBAGE && cntAnchor < 2) {
      obj->setHasMultiRef(false);
      if (cntAnchor == 0) {
        obj->_refs = 0;
      }
      else {
        GCObject* remained = referrers->at(0);
        obj->_refs = _pointer2offset(remained);
      }
      _rtgc.gRefListPool.delete_(referrers);
    }
    else if (cntAnchor != referrers->size()) {
      referrers->resize(cntAnchor);
    }
  }
  else {
    if (!adjust_anchor_pointer((ShortOOP*)&obj->_refs, obj) && CHECK_GARBAGE) {
      check_shortcut = true;
      obj->_refs = 0;
    }
  }

  if (check_shortcut) {
    int s_id = obj->getShortcutId();
    if (s_id > INVALID_SHORTCUT) {
      rtgc_log(true, "broken shortcut found [%d] %p\n", s_id, obj);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      SafeShortcut* ss = obj->getShortcut();
      if (ss->tail() != obj) {
        // adjust_point 가 완료되지 않아 validateShortcut() 실행 불가.
        ss->anchor_ref() = obj;
      } else {
        delete ss;
      }
      obj->setShortcutId_unsafe(obj->hasReferrer() ? INVALID_SHORTCUT : 0);
    }
  }
}

void rtHeap::refresh_young_roots() {
  /**
   * @brief 
   * full-gc : 객체를 이동할 주소를 모두 설정한 후 호출된다.
   * 객체 이동 전에 리스트에서 가비지 객체를 제거하여야 한다. 
   * yg-gc : 현재 사용하지 않음.
   */

  rtgc_log(LOG_OPT(11), "refresh_young_roots %d\n", 1);

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
  rtgc_log(LOG_OPT(8), "refresh_young_roots %d->%d\n", 
      g_young_roots.length(), cnt_young_root);
  g_young_roots.trunc_to(cnt_young_root);
}

template <typename T>
void RtAnchorRemoveClosure::do_oop_work(T* p) {
  T heap_oop = RawAccess<>::oop_load(p);
  if (!CompressedOops::is_null(heap_oop)) {
    oop obj = CompressedOops::decode_not_null(heap_oop);
    if (obj->is_gc_marked() && to_obj(obj)->hasReferrer()) {
      rtgc_log(true, "remove garbage anchor(%p) from %p\n", _garbage_anchor, (void*)obj);
      to_obj(obj)->removeMatchedReferrers(_garbage_anchor);
    }
  }
}

static bool is_dead_space(GCObject* node) {
  return node->isGarbageMarked(); // && klass == int[] || klass == Objet_class()
}

void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
  
  assert(node->getRootRefCount() == 0, "wrong refCount(%d) on garbage %p(%s)\n", 
      node->getRootRefCount(), node, RTGC::getClassName(node));
  assert(check_garbage(node, false), "invalid trackable garbage %p, yg-r=%d, rc=%d:%d\n",
      node, node->isYoungRoot(), node->getRootRefCount(), node->hasReferrer());

  if (node->hasMultiRef()) {
    node->removeAnchorList();
  }
  if (!node->isTrackable()) {
    precond(node->getShortcutId() == 0);
    return;
  }

  debug_only(g_cntTrackable --);
  int s_id = node->getShortcutId();
  if (s_id > INVALID_SHORTCUT) {
    SafeShortcut* ss = node->getShortcut();
    if (ss->tail() == node) {
      // rtgc_log(true, "garbage shortcut found [%d] %p\n", s_id, node);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      delete ss;
    }
  }

  //RtAnchorRemoveClosure r(node);
  //cast_to_oop(node)->oop_iterate(&r);
  node->markGarbage();
}

void rtHeap::prepare_full_gc() {
  int len = g_keep_alives.length();
  if (len > 0) {
    oop* p = g_keep_alives.adr_at(0);
    oop* end = p + len;
    for (; p < end; p ++) {
      GCObject* node = to_obj(*p);
      precond(node->isTrackable());
      node->unmarkKeepAlive();
      postcond(!node->isKeepAlive());
    }
  }
  rtgc_log(LOG_OPT(1), "clear keep_alives %d\n", g_keep_alives.length());
  g_keep_alives.trunc_to(0);
}


void __discover_java_references(ReferenceDiscoverer* rp, bool is_full_gc) {
  precond(rp != NULL);
  oop prev = NULL;
  oop ref = g_phantom_ref;
  oop next_ref;
  oop discovered_list = NULL;
  const int referent_off = java_lang_ref_Reference::referent_offset();

  for (; ref != NULL; ref = next_ref) {
    next_ref = RawAccess<>::oop_load(java_lang_ref_Reference::discovered_addr_raw(ref));
    rtgc_log(true, "phantom ref %p %d\n", (void*)ref, ref->is_gc_marked());
    precond(ref != next_ref);
    if (!ref->is_gc_marked()) continue;
    oop new_ref = cast_to_oop(RTGC::getForwardee(to_obj(ref)));
    rtgc_log(true, "phantom ref %p moved %p\n", (void*)ref, (void*)new_ref);

    oop referent = RawAccess<>::oop_load(java_lang_ref_Reference::referent_addr_raw(ref));
    if (referent == NULL) {
      rtgc_log(true, "referent cleaned %p (maybe by PhantomCleanable)\n", (void*)ref);
      continue;
    }

    if (!referent->is_gc_marked()) {
      if (!is_full_gc) ref = new_ref;
      rtgc_log(true, "reference %p has garbafe referent\n", (void*)ref);
      java_lang_ref_Reference::set_discovered_raw(ref, oop(NULL));//discovered_list);
      rp->discover_reference(ref, REF_WEAK/*trick*/);
      continue;
    }

    if (prev == NULL) {
      g_phantom_ref = new_ref;
    } else {
      java_lang_ref_Reference::set_discovered_raw(prev, new_ref);
    }
    prev = is_full_gc ? ref : new_ref;
    oop new_referent = cast_to_oop(RTGC::getForwardee(to_obj(referent)));
    rtgc_log(true, "referent marked %p -> %p\n", (void*)referent, (void*)new_referent);
    if (referent != new_referent) {
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(prev, referent_off, new_referent);
    }
  }
  if (prev == NULL) {
    g_phantom_ref = NULL;
  }
    // oop old = Universe::swap_reference_pending_list(_refs_list.head());
    // HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(_prev_discovered, java_lang_ref_Reference::discovered_offset(), old);

}


void rtHeap::discover_java_references(ReferenceDiscoverer* rp, bool is_tenure_gc) {
  if (RTGC_CHECK_EMPTY_TRACKBLE) {
    assert(empty_trackable == NULL, "empty_trackable is not catched!");
  }

  if (is_tenure_gc) {
    GCRuntime::adjustShortcutPoints();
  } else {
    precond(g_pending_trackables.length() == 0);
    rtHeap__clear_garbage_young_roots();
  }

  g_garbage_list.resize(0);
#if RTGC_OPT_PHANTOM_REF  
  __discover_java_references(rp, is_tenure_gc);
#endif

  if (!USE_PENDING_TRACKABLES) return;
  const int count = g_pending_trackables.length();
  rtgc_log(LOG_OPT(11), "finish_collection %d\n", count);
  if (count == 0) return;

  oop* pOop = &g_pending_trackables.at(0);
  for (int i = count; --i >= 0; ) {
    oopDesc* p = *pOop++;
    RTGC::iterateReferents(to_obj(p), (RefTracer2)RTGC::add_referrer_unsafe, p);
  }
  g_pending_trackables.trunc_to(0);
  return;
}

void rtHeap::release_jni_handle(oopDesc* p) {
  assert(to_obj(p)->getRootRefCount() > 0, "wrong handle %p\n", p);
  rtgc_log(p == RTGC::debug_obj, "release_handle %p\n", p);
  GCRuntime::onEraseRootVariable_internal(to_obj(p));
}

void rtHeap::init_java_reference(oopDesc* ref_oop, oopDesc* referent) {
  if (referent == NULL) return;

  ptrdiff_t referent_offset = java_lang_ref_Reference::referent_offset();  
  HeapAccess<>::oop_store_at(ref_oop, referent_offset, referent);
#ifdef ASSERT  
  oop check = RawAccess<>::oop_load(java_lang_ref_Reference::referent_addr_raw(ref_oop));
  postcond(check != NULL);
#endif  
  if (!java_lang_ref_Reference::is_phantom(ref_oop)) {
    return;
  }
  rtgc_log(true, "created phantom %p for %p\n", (void*)ref_oop, referent);

  oop next_discovered = Atomic::xchg(&g_phantom_ref, ref_oop);
  java_lang_ref_Reference::set_discovered_raw(ref_oop, next_discovered);
  return;
}



void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.length(), full_gc); 
}

