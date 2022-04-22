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

    bool is_in_young(void* p) { return p < _old_gen_start; }
    void init(oopDesc* old_anchor_p, oopDesc* new_anchor_p, bool is_java_reference) { 
      _old_anchor_p = old_anchor_p; 
      _new_anchor_p = new_anchor_p; 
      _has_young_ref = false; 
    }
    bool _has_young_ref;
    HeapWord* _old_gen_start;
  private:
    oopDesc* _old_anchor_p;
    oopDesc* _new_anchor_p;
  };

  
  GrowableArrayCHeap<oop, mtGC> g_pending_trackables;
  GrowableArrayCHeap<oop, mtGC> g_young_roots;
  GrowableArrayCHeap<oop, mtGC> g_keep_alives;
  GrowableArrayCHeap<GCNode*, mtGC> g_stack_roots;
  SimpleVector<GCObject*> g_garbage_list;
  Thread* gcThread = NULL;
  int g_cntTrackable = 0;
  int g_cntScan = 0;
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

static bool is_java_reference(oopDesc* obj, ReferenceType rt) {
  return obj->klass()->id() == InstanceRefKlassID && 
        ((InstanceRefKlass*)obj->klass())->reference_type() == rt;
}


// static bool is_java_reference(oopDesc* obj) {
//   return (obj->klass()->id() == InstanceRefKlassID);
// }

// static bool is_java_reference_with_young_referent(oopDesc* obj) {
//   if (!is_java_reference(obj)) return false;
//   oop referent = java_lang_ref_Reference::unknown_referent_no_keepalive(obj);
//   return referent != NULL && !to_obj(referent)->isTrackable();
// }


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


bool rtHeap::is_alive(oopDesc* p, bool assert_alive) {
  if (assert_alive) {
    assert(!to_obj(p)->isGarbageMarked(), "invalid pointer %p(%s) isClass=%d\n", 
        p, RTGC::getClassName(to_obj(p)), p->klass() == vmClasses::Class_klass());
  }
  return !to_obj(p)->isGarbageMarked();
}



static bool is_dead_space(oopDesc* obj) {
  Klass* klass = obj->klass();
  return klass->is_typeArray_klass() || klass == vmClasses::Object_klass();
}

static void ensure_alive_or_deadsapce(oopDesc* old_p) {
  assert(!to_obj(old_p)->isGarbageMarked() || is_dead_space(old_p), 
        "invalid pointer %p(%s) isClass=%d\n", 
        old_p, RTGC::getClassName(to_obj(old_p)), old_p->klass() == vmClasses::Class_klass());
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
  bool dead_space = SafepointSynchronize::is_at_safepoint()
      && Thread::current()->is_VM_thread();
  if (!dead_space) {
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

template <bool is_full_gc>
static oopDesc* get_valid_forwardee(oopDesc* obj) {
  if (is_full_gc) {
    if (to_obj(obj)->isGarbageMarked()) {
      assert(!obj->is_gc_marked() || is_dead_space(obj), "wrong garbage mark on %p(%s)\n", 
          obj, RTGC::getClassName(to_obj(obj)));
      return NULL;
    } else {
      precond(obj->is_gc_marked());
      oopDesc* p = obj->forwardee();
      return (p == NULL) ? obj : p;
    }
  } 

  if (to_obj(obj)->isTrackable()) {
    if (to_obj(obj)->isGarbageMarked()) return NULL;
    return obj;
  }

  if (!obj->is_gc_marked()) return NULL;
  postcond(!to_obj(obj)->isGarbageMarked());
  return obj->forwardee();
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
    if (anchor->isGarbageMarked() || is_dead_space(cast_to_oop(anchor))) continue;
    if (checkByGarbageMark) {
      if (!anchor->isGarbageMarked()) {
        rtgc_log(true, "invalid anchor of garbage %p(%s), yg-r=%d, rc=%d:%d\n",
              anchor, RTGC::getClassName(anchor), anchor->isYoungRoot(), anchor->getRootRefCount(), anchor->hasReferrer());
        return false;
      }
    }
    else if (IS_GC_MARKED(cast_to_oop(anchor)) 
      && cast_to_oop(anchor)->klass()->id() != InstanceRefKlassID) {
        rtgc_log(true, "invalid anchor yg-root %p(%s), yg-r=%d, rc=%d:%d\n",
              anchor, RTGC::getClassName(anchor), anchor->isYoungRoot(), anchor->getRootRefCount(), anchor->hasReferrer());
        return false;
    }
  }
  return true;
}
#endif

void rtHeap__clear_garbage_young_roots() {
  int cnt_root = g_saved_young_root_count;
  if (cnt_root > 0) {
    oop* src_0 = g_young_roots.adr_at(0);
    RTGC::collectGarbage(reinterpret_cast<GCObject**>(src_0), cnt_root);
    oop* dst = src_0;
    oop* end = src_0 + cnt_root;
    // for (oop* src = src_0; src < end; src++) {
    //   GCObject* node = to_obj(*src);
    //   if (node->isGarbageMarked()) {
    //     assert(check_garbage(node, false), "invalid yg-root %p, yg-r=%d, rc=%x:%d\n",
    //           node, node->isYoungRoot(), node->getRootRefCount(), node->hasReferrer());
    //   } else if (node->isUnsafe() && 
    //     GarbageProcessor::detectGarbage(node)) {
    //     rtgc_log(LOG_OPT(11), "garbage YG Root %p(%s)\n", (void*)node, RTGC::getClassName(node));
    //   }  
    // }

    // 1차 검사시 unsafe 상태가 아니었으나, 다른 root 객체를 scan 하는 도중 garbage 로 마킹된 root 객체가 있을 수 있다.
    // 이에 가비지 검색 종료 후, 다시 가비지 여부를 판별하여야 한다.
    for (oop* src = src_0; src < end; src++) {
      GCObject* node = to_obj(*src);
      if (!node->isGarbageMarked() && node->isYoungRoot()) {
        if (dst != src) {
          *dst = cast_to_oop(node);
        }
        dst ++;
      }
    }

    int remain_roots = dst - src_0;
    int new_roots = g_young_roots.length() - cnt_root;
    if (new_roots > 0) {
      int space = cnt_root - remain_roots;
      if (space >= new_roots) {
        memcpy(dst, g_young_roots.adr_at(cnt_root), new_roots * sizeof(void*));
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

#ifdef ASSERT
  if ((cnt_root = g_young_roots.length()) > 0) {
    oop* src = g_young_roots.adr_at(0);
    oop* end = src + cnt_root;
    for (; src < end; src++) {
      GCObject* node = to_obj(*src);
      precond(!node->isGarbageMarked());
    }
  }
#endif

  if ((cnt_root = g_stack_roots.length()) > 0) {
    GCNode** src = &g_stack_roots.at(0);
    GCNode** end = src + cnt_root;
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
    assert(!node->isGarbageMarked(), "invalid yg-root %p(%s)\n", node, RTGC::getClassName(node));
    if (!node->isAnchored()) {
      node->markGarbage();
      rtgc_log(LOG_OPT(3), "skip garbage node %p\n", (void*)node);
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
  if (anchor == link) return;

  assert(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)\n", anchor, anchor->klass()->name()->bytes());
  if (young_ref_reahcable) {
    rtHeap::mark_survivor_reachable(link);
  } else {
    precond(!to_obj(link)->isGarbageMarked());
  }

  RTGC::add_referrer_unsafe(link, anchor);
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

template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  // rtgc_log(is_java_reference(_old_anchor_p, REF_PHANTOM), 
  //     "reference(%p)  discovered(%p) moved -> %p:%ld/%d\n", _old_anchor_p, (void*)old_p, (void*)new_p,
  //         ((address)p - (address)_old_anchor_p), java_lang_ref_Reference::discovered_offset());
  if (old_p == NULL) return;

#ifdef ASSERT
  ensure_alive_or_deadsapce(old_p);
  RTGC::adjust_debug_pointer(old_p, new_p);
#endif   

  _has_young_ref |= is_in_young(new_p);
  if (_new_anchor_p == NULL) return;
  if (USE_PENDING_TRACKABLES) return;

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


static bool adjust_anchor_pointer(ShortOOP* p, GCObject* node) {
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

static void __adjust_anchor_pointers(oopDesc* old_p) {
  precond(old_p->is_gc_marked() || 
      (old_p->forwardee() == NULL && !RTGC_REMOVE_GARBAGE_REFERRER_ON_ADJUST_POINTER));

  GCObject* obj = to_obj(old_p);
  precond(!obj->isGarbageMarked());

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
      rtgc_log(LOG_OPT(9), "broken shortcut found [%d] %p\n", s_id, obj);
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

size_t rtHeap::adjust_pointers(oopDesc* old_p) {
  if (!is_alive(old_p)) {
    precond(!old_p->is_gc_marked() || is_dead_space(old_p));
    int size = old_p->size_given_klass(old_p->klass());
    return size;
  }

#ifdef ASSERT
  g_cntScan ++;
#endif

  oopDesc* new_anchor_p = NULL;
  bool is_java_ref = false;
  if (!to_obj(old_p)->isTrackable()) {
    oopDesc* p = old_p->forwardee();
    if (p == NULL) p = old_p;
    if (!g_adjust_pointer_closure.is_in_young(p)) {
      mark_pending_trackable(old_p, p);
      new_anchor_p = p;
    }
  }
  rtgc_debug_log(old_p, 
      "adjust_pointers %p->%p\n", (void*)old_p, get_valid_forwardee<true>(old_p));

  g_adjust_pointer_closure.init(old_p, new_anchor_p, is_java_ref);
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool is_young_root = g_adjust_pointer_closure._has_young_ref && to_obj(old_p)->isTrackable();
  if (is_young_root) {
    oopDesc* forwardee = old_p->forwardee();
    if (forwardee == NULL) forwardee = old_p;
    add_young_root(old_p, forwardee);
  }

  __adjust_anchor_pointers(old_p); 

  if (!USE_PENDING_TRACKABLES) {
    to_obj(old_p)->unmarkDirtyReferrerPoints();
  }
  return size; 
}

void rtHeap::prepare_point_adjustment(void* old_gen_heap_start) {
  if (g_adjust_pointer_closure._old_gen_start != NULL) return;

  g_adjust_pointer_closure._old_gen_start = (HeapWord*)old_gen_heap_start;
  oop* src_0 = g_young_roots.adr_at(0);
  oop* dst = src_0;
  oop* end = src_0 + g_young_roots.length();
  for (oop* src = src_0; src < end; src++) {
    GCObject* node = to_obj(*src);
    node->unmarkYoungRoot();
  }
  g_young_roots.trunc_to(0);
}

void GCNode::markGarbage()  {
    assert(this->isTrackable() || is_dead_space(cast_to_oop(this)),
        "invalid garbage marking on %p(%s)\n", this, getClassName(this));
    assert(!cast_to_oop(this)->is_gc_marked(),
        "invalid garbage marking on %p(%s)\n", this, getClassName(this));
		_flags.isGarbage = true;
}

void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
  
  assert(node->getRootRefCount() == 0, "wrong refCount(%d) on garbage %p(%s)\n", 
      node->getRootRefCount(), node, RTGC::getClassName(node));
  assert(check_garbage(node, false), "invalid trackable garbage %p, yg-r=%d, rc=%d:%d\n",
      node, node->isYoungRoot(), node->getRootRefCount(), node->hasReferrer());
  rtgc_log(LOG_OPT(11), "trackable destroyed %p, yg-r=%d\n", node, node->isYoungRoot());

  if (node->hasMultiRef()) {
    node->removeAnchorList();
  }

  if (!node->isTrackable()) {
    precond(node->getShortcutId() == 0);
    return;
  }

  int s_id = node->getShortcutId();
  if (s_id > INVALID_SHORTCUT) {
    SafeShortcut* ss = node->getShortcut();
    if (ss->tail() == node) {
      // rtgc_log(true, "garbage shortcut found [%d] %p\n", s_id, node);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      delete ss;
    }
  }

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

template <bool is_full_gc>
static void __discover_java_references(ReferenceDiscoverer* rp);

void rtHeap::discover_java_references(ReferenceDiscoverer* rp, bool is_tenure_gc) {
  if (RTGC_CHECK_EMPTY_TRACKBLE) {
    assert(empty_trackable == NULL, "empty_trackable is not catched!");
  }

  if (is_tenure_gc) {
    GCRuntime::adjustShortcutPoints();
  }

  if (is_tenure_gc) {
    __discover_java_references<true>(rp);
  } else {
    __discover_java_references<false>(rp);
    rtHeap__clear_garbage_young_roots();
  }

  g_adjust_pointer_closure._old_gen_start = NULL;

  g_garbage_list.resize(0);
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
  if (!java_lang_ref_Reference::is_phantom(ref_oop)) {
    HeapAccess<>::oop_store_at(ref_oop, referent_offset, referent);
    rtgc_log(false && ((InstanceRefKlass*)ref_oop->klass())->reference_type() == REF_WEAK, 
          "weak ref %p for %p\n", (void*)ref_oop, referent);
    return;
  }

  rtgc_log(LOG_OPT(3), "created phantom %p for %p\n", (void*)ref_oop, referent);
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_oop, referent_offset, referent);
  oop next_discovered = Atomic::xchg(&g_phantom_ref, ref_oop);
  java_lang_ref_Reference::set_discovered_raw(ref_oop, next_discovered);
  return;
}

void rtHeap::link_discovered_pending_reference(oopDesc* ref_oop, oopDesc* discovered) {
  precond(ref_oop != discovered);
  if (to_obj(ref_oop)->isTrackable()) {
    RTGC::add_referrer_unsafe(discovered, ref_oop);
  }
}

template <bool is_full_gc>
void __discover_java_references(ReferenceDiscoverer* rp) {
  precond(rp != NULL);
  oop last_ref = NULL;
  oop next_ref;
  oop pending_head = NULL;
  oop pending_tail = NULL;
  oop alive_head = NULL;
  const int referent_off = java_lang_ref_Reference::referent_offset();
  const int discovered_off = java_lang_ref_Reference::discovered_offset();

#ifdef ASSERT
  int cnt_garbage = 0;
  int cnt_phantom = 0;
  int cnt_pending = 0;
  int cnt_cleared = 0;
  int cnt_alive = 0;
#endif
  
  for (oop ref = g_phantom_ref; ref != NULL; ref = next_ref) {
    rtgc_log(LOG_OPT(3), "check phantom ref %p\n", (void*)ref);
    next_ref = RawAccess<>::oop_load_at(ref, discovered_off);
    precond(ref != next_ref);
    oop new_ref = get_valid_forwardee<is_full_gc>(ref);
    if (new_ref == NULL) {
      rtgc_log(LOG_OPT(3), "garbage phantom ref %p removed\n", (void*)ref);
      // to_obj(ref)->markGarbage();
      debug_only(cnt_garbage++;)
      continue;
    }

    rtgc_log(LOG_OPT(3), "phantom ref %p moved %p\n", (void*)ref, (void*)new_ref);
    oop referent = RawAccess<>::oop_load_at(ref, referent_off);
    if (referent == NULL) {
      debug_only(cnt_cleared++;)
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, discovered_off, oop(NULL));
      rtgc_log(LOG_OPT(3), "referent cleaned %p (maybe by PhantomCleanable)\n", (void*)ref);
      continue;
    }

    last_ref = is_full_gc ? ref : new_ref;
    oop new_referent = get_valid_forwardee<is_full_gc>(referent);
    if (new_referent == NULL) {
      debug_only(cnt_pending++;)
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(last_ref, referent_off, oop(NULL));
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(last_ref, discovered_off, oop(NULL));
      HeapAccess<>::oop_store_at(last_ref, discovered_off, pending_head);
      rtgc_log(LOG_OPT(3), "reference %p(->%p) with garbage referent linked (%p)\n", 
            (void*)ref, (void*)new_ref, (void*)pending_head);
      if (pending_tail == NULL) {
        precond(pending_head == NULL);
        pending_tail = last_ref;
      }
      pending_head = new_ref;
      continue;
    }

    rtgc_log(LOG_OPT(3), "alive reference %p(->%p) linked (%p)\n", 
          (void*)ref, (void*)new_ref, (void*)alive_head);
    java_lang_ref_Reference::set_discovered_raw(last_ref, alive_head);
    alive_head = new_ref;
    debug_only(cnt_alive++;)

    rtgc_log(LOG_OPT(3), "referent of (%p) marked %p -> %p\n", (void*)ref, (void*)referent, (void*)new_referent);
    if (referent != new_referent) {
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(last_ref, referent_off, new_referent);
    }
  }

  g_phantom_ref = alive_head;
  rtgc_log(LOG_OPT(3), "total phatom scanned %d, garbage %d, cleared %d, pending %d, alive %d q=%p\n",
        cnt_phantom, cnt_garbage, cnt_cleared, cnt_pending, cnt_alive, (void*)alive_head);

  if (pending_head != NULL) {
    oop old = Universe::swap_reference_pending_list(pending_head);
    HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail, discovered_off, oop(NULL));
    HeapAccess<>::oop_store_at(pending_tail, discovered_off, old);
  }
}

void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.length(), full_gc); 
}
