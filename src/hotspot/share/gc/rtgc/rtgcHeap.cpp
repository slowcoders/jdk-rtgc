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
#include "gc/shared/genCollectedHeap.hpp"
#include "gc/shared/weakProcessor.hpp"
#include "gc/shared/genOopClosures.inline.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

#include "rtRefProcessor.hpp"

bool rtHeap::in_full_gc = false;

using namespace rtHeapUtil;

namespace RTGC {
  // bool yg_root_locked = false;
  extern bool REF_LINK_ENABLED;
  bool ENABLE_GC = true && REF_LINK_ENABLED;
  class RtAdjustPointerClosure: public BasicOopIterateClosure {
  public:
    template <typename T> void do_oop_work(T* p);
    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
    virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }

    bool is_in_young(void* p) { return p < _old_gen_start; }
    void init(oopDesc* old_anchor_p, oopDesc* new_anchor_p) { 
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


  HugeArray<oop> g_young_roots;
  HugeArray<GCObject*> g_stack_roots;
  int g_resurrected_top = INT_MAX;
  Thread* gcThread = NULL;
  int g_cntTrackable = 0;
  int g_cntScan = 0;
  int g_saved_young_root_count = 0;
  RtAdjustPointerClosure g_adjust_pointer_closure;
  RtYoungRootClosure* g_young_root_closure;
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
        (rt == (ReferenceType)-1 || ((InstanceRefKlass*)obj->klass())->reference_type() == rt);
}

static oopDesc* __get_discovered(oop obj) {
  return obj->klass()->id() != InstanceRefKlassID ? NULL
    : java_lang_ref_Reference::discovered(obj);
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
//   if (node->isStrongRootReachable()) return;
//   node->incrementRootRefCount();

//   AnchorIterator it(node);
//   while (it.hasNext()) {
//     GCObject* anchor = it.next();
//     dump_anchor_tree(depth + 1, anchor);
//   }
// }

bool rtHeap::is_alive(oopDesc* p) {
  GCObject* node = to_obj(p);
  bool alive = !node->isGarbageMarked() && (node->isTrackable() || p->is_gc_marked());
  return alive;
}





void rtHeap::add_young_root(oopDesc* old_p, oopDesc* new_p) {
  precond(to_obj(old_p)->isTrackable());
  precond(!to_obj(old_p)->isGarbageMarked());
  // precond(g_young_roots.indexOf(old_p) < 0);
  // precond(g_young_roots.indexOf(new_p) < 0);
  to_obj(old_p)->markYoungRoot();
  g_young_roots.push_back(new_p);
  rtgc_debug_log(old_p, "mark YG Root (%p)->%p idx=%d\n", old_p, new_p, g_young_roots.size());
}

bool rtHeap::is_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isTrackable();
}

void rtHeap::mark_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onAssignRootVariable_internal(obj);
}

void rtHeap::clear_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onEraseRootVariable_internal(obj);
}

bool rtHeap::ensure_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isStrongRootReachable();
}


void rtHeap::mark_promoted_trackable(oopDesc* new_p) {
  // YG GC 수행 도중에 old-g로 옮겨진 객체들을 marking 한다.
  precond(!to_obj(new_p)->isTrackable());
  to_obj(new_p)->markTrackable();
}

void rtHeap::mark_tenured_trackable(oopDesc* new_p) {
  // YG GC 수행 전에, old-heap 에 allocate 된 객체들을 marking 한다.
  precond (!to_obj(new_p)->isTrackable());
  to_obj(new_p)->markTrackable();
  GCRuntime::detectUnsafeObject(to_obj(new_p));
}

static void resurrect_young_root(GCObject* node) {
  precond(node->isGarbageMarked());
  precond(node->isTrackable());
  node->unmarkGarbage();
  node->unmarkDirtyReferrerPoints();  
  oop anchor = g_young_root_closure->current_anchor();
  if (anchor != NULL) {
    precond(!node->hasSafeAnchor());
    node->setSafeAnchor(to_obj(anchor));
    node->setShortcutId_unsafe(INVALID_SHORTCUT);
  }
  rtgc_log(LOG_OPT(11), "resurrect obj %p -> %p  root=%d\n", 
      (void*)anchor, node, node->isYoungRoot());
  if (!g_young_root_closure->iterate_tenured_young_root_oop(cast_to_oop(node))) {
    if (node->isYoungRoot()) {
      node->unmarkYoungRoot();
    }
  } else if (!node->isYoungRoot()) {
    rtHeap::add_young_root(cast_to_oop(node), cast_to_oop(node));
  }
}

void rtHeap__addRootStack_unsafe(GCObject* node) {
  g_stack_roots.push_back(node);
}

void rtHeap__addResurrectedObject(GCObject* node) {
  if (g_resurrected_top == INT_MAX) {
    g_resurrected_top = g_stack_roots.size();
  }
  g_stack_roots.push_back(node);
}


void rtHeap::mark_survivor_reachable(oopDesc* new_p) {
  GCObject* node = to_obj(new_p);
  assert(node->isTrackable(), "must be trackable %p(%s)\n", new_p, RTGC::getClassName(to_obj(new_p)));
  if (node->isGarbageMarked()) {
    assert(node->isTrackable(), "no y-root %p(%s)\n",
        node, RTGC::getClassName(node));
    resurrect_young_root(node);
    if (node->hasSafeAnchor()) return;
    // garbage marking 된 상태는 stack marking 이 끝난 상태.
  }
  if (!node->isStrongRootReachable()) {
    rtgc_log(LOG_OPT(9), "add stack root %p\n", new_p);
    GCRuntime::onAssignRootVariable_internal(node);
    g_stack_roots.push_back(node);
  }
}


template<bool is_full_gc>
void rtHeap__clearStack() {
  int cnt_root = g_stack_roots.size();
  if (cnt_root > 0) {
    GCObject** src = &g_stack_roots.at(0);
    GCObject** end = src + cnt_root;
    rtgc_log(LOG_OPT(8), "clear_stack_roots %d\n", 
        g_stack_roots.size());
    for (; src < end; src++) {
      GCObject* erased = src[0];
      precond(erased->isTrackable());
      if (!is_full_gc) {
        GCRuntime::onEraseRootVariable_internal(erased);
      }
      else if (erased->decrementRootRefCount() <= ZERO_ROOT_REF) {
        if (erased->isUnsafeTrackable() && !erased->isUnstableMarked()) {
          erased->markUnstable();
          rtgc_debug_log(erased, "add unsafe=%p\n", erased);
          if (is_full_gc) {
            oop new_p = cast_to_oop(erased)->forwardee();
            erased = new_p == NULL ? erased : to_obj(new_p);
          }
          _rtgc.g_pGarbageProcessor->addUnstable_ex(erased);
        }
      }
    }
    rtgc_log(LOG_OPT(8), "iterate_stack_roots done %d\n", 
        g_stack_roots.size());
    g_stack_roots.resize(0);
  }
}


void rtHeap__clear_garbage_young_roots(bool is_full_gc) {
  if (!is_full_gc) {
    _rtgc.g_pGarbageProcessor->validateGarbageList();
  }
  _rtgc.g_pGarbageProcessor->collectGarbage(is_full_gc);

  int old_cnt = g_young_roots.size();
  for (int i = old_cnt; --i >= 0; ) {
    GCObject* node = to_obj(g_young_roots.at(i));
    if (node->isGarbageMarked() || !node->isYoungRoot()) {
      g_young_roots.removeFast(i);
    } else if (is_full_gc) {
      precond(cast_to_oop(node)->is_gc_marked());
    }
  }

  if (!is_full_gc) {
    rtHeap__clearStack<false>();
  }
  rtgc_log(LOG_OPT(8), "rtHeap__clear_garbage_young_roots done %d->%d\n", 
      old_cnt, g_young_roots.size());
}



void rtHeap::iterate_younger_gen_roots(RtYoungRootClosure* closure, bool is_full_gc) {
  if (is_full_gc) {
    if (closure != NULL) {
      g_young_root_closure = closure;
      return;
    } 
    closure = g_young_root_closure;
    precond(closure != NULL);
  } else {
    g_young_root_closure = closure;
  }
  int young_root_count = is_full_gc ? g_young_roots.size() : g_saved_young_root_count;

#ifdef ASSERT
  for (int idx_root = young_root_count; --idx_root >= 0; ) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
    assert(!node->isGarbageMarked(), "invalid yg-root %p(%s)\n", node, RTGC::getClassName(node));
  }
#endif

  HugeArray<GCObject*>* garbages = _rtgc.g_pGarbageProcessor->getGarbageNodes(); 
  precond(is_full_gc || garbages->size() == 0);
  for (int idx_root = young_root_count; --idx_root >= 0; ) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
    if (node->isGarbageMarked()) {
      // ref-pending-list 를 추가하는 과정에서, yg-root 에서 제거된 객체가 
      // 다시 추가될 수 있다.
      precond(is_full_gc || node->isDirtyReferrerPoints());
      continue;
    }
    assert(node->isYoungRoot(), "invalid young root %p\n", node);
    if (is_full_gc) {
      if (_rtgc.g_pGarbageProcessor->detectGarbage(node)) continue;
    } else if (node->isUnreachable()) {
      node->markGarbage("unreachable young root");
      // -> FieldIterator 에서 yg-root garbage 의 field 가 forwarded 값인 가를 확인하기 위하여 사용한다.
      node->markDirtyReferrerPoints();
      garbages->push_back(node);
      rtgc_log(false, "skip garbage yg-root %p\n", node);
      continue;
    }

    rtgc_log(LOG_OPT(8), "iterate yg root %p\n", (void*)node);
    bool is_root = closure->iterate_tenured_young_root_oop(cast_to_oop(node));
    closure->do_complete();
    if (!is_root) {
      node->unmarkYoungRoot();
      g_young_roots.removeFast(idx_root);
    } 
  }
}


void rtHeap::add_trackable_link(oopDesc* anchor, oopDesc* link) {
  if (anchor == link) return;
  GCObject* node = to_obj(link);
  rtgc_debug_log(node, "add link %p -> %p\n", anchor, link);
  assert(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)\n", anchor, anchor->klass()->name()->bytes());
  if (node->isGarbageMarked()) {
    assert(node->hasReferrer() || (node->isYoungRoot() && node->isDirtyReferrerPoints()), 
        "invalid link %p(%s) -> %p(%s)\n", 
        anchor, RTGC::getClassName(to_obj(anchor)), node, RTGC::getClassName(node));

    assert(in_full_gc || node->isYoungRoot(), "no y-root %p(%s)\n",
        node, RTGC::getClassName(node));
    resurrect_young_root(node);
  }

  precond(to_obj(anchor)->isTrackable() && !to_obj(anchor)->isGarbageMarked());
  RTGC::add_referrer_ex(link, anchor, false);
}

void rtHeap::mark_forwarded(oopDesc* p) {
  // rtgc_log(LOG_OPT(4), "marked %p\n", p);
  GCObject* node = to_obj(p);
  precond(!node->isGarbageMarked());
  assert(!node->isTrackable() || 
    node->isStrongRootReachable() || 
    ((false && node->isPublished()) ? node->hasSafeAnchor() : node->hasReferrer()) ||
    node->isUnstableMarked(),
      " invalid node %p(%s) rc=%d, safeAnchor=%d unsafe=%d\n", 
      node, RTGC::getClassName(node),
      node->getRootRefCount(), node->hasSafeAnchor(), node->isUnstableMarked());
  // assert(!to_node(p)->isUnstableMarked(), "unstable forwarded %p(%s)\n", p, getClassName(to_obj(p)));
  // TODO markDirty 시점이 너무 이름. 필요없다??
  node->markDirtyReferrerPoints();
}


template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  if (old_p == NULL || old_p == _old_anchor_p) return;

#ifdef ASSERT
  ensure_alive_or_deadsapce(old_p, _old_anchor_p);
  RTGC::adjust_debug_pointer(old_p, new_p, false);
#endif   

  _has_young_ref |= is_in_young(new_p);
  if (_new_anchor_p == NULL) return;

  // _old_anchor_p 는 old-address를 가지고 있으므로, Young root로 등록할 수 없다.
  if (to_obj(old_p)->isDirtyReferrerPoints()) {
    // old_p 에 대해 adjust_pointers 를 수행하기 전.
    //RTGC::add_referrer_ex(old_p, _old_anchor_p, false);
    RTGC::add_referrer_unsafe(old_p, _old_anchor_p, _old_anchor_p);
  }
  else {
    // old_p 에 대해 이미 adjust_pointers 가 수행됨.
    RTGC::add_referrer_unsafe(old_p, _new_anchor_p, _old_anchor_p);
  }
}


static bool adjust_anchor_pointer(ShortOOP* p, GCObject* node) {
  GCObject* old_p = p[0];
  if (old_p->isGarbageMarked()) {
    rtgc_log(LOG_OPT(11), "garbage anchor %p in %p\n", old_p, node);
    return false;
  }

  GCObject* new_obj = (GCObject*)cast_to_oop(old_p)->mark().decode_pointer();
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
      (old_p->forwardee() == NULL && !RtLateClearGcMark));

  GCObject* obj = to_obj(old_p);
  precond(!obj->isGarbageMarked());

  const bool CHECK_GARBAGE = RtLateClearGcMark;
  bool check_shortcut;

  if (!obj->hasReferrer()) {
    check_shortcut = true;
  }
  else if (obj->hasMultiRef()) {
    ReferrerList* referrers = obj->getReferrerList();
    ShortOOP* ppAnchor = referrers->adr_at(0);
    int cntAnchor = referrers->size();
    check_shortcut = ppAnchor[0]->isGarbageMarked();
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
        rtgc_debug_log(obj, "anchor-list cleared %p\n", obj);
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
    check_shortcut = (!adjust_anchor_pointer((ShortOOP*)&obj->_refs, obj) && CHECK_GARBAGE);
    if (check_shortcut) {
      obj->_refs = 0;
      rtgc_debug_log(obj, "single anchor cleared %p\n", obj);
    }
  }

  if (check_shortcut) {
    if (obj->hasShortcut()) {
      rtgc_log(LOG_OPT(9), "broken shortcut found [%d] %p\n", obj->getShortcutId(), obj);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      SafeShortcut* ss = obj->getShortcut();
      if (ss->tail() != obj) {
        // adjust_point 가 완료되지 않아 validateShortcut() 실행 불가.
        ss->anchor_ref() = obj;
      } else {
        delete ss;
      }
      obj->invalidateSafeAnchor();
    }
  }
}


size_t rtHeap::adjust_pointers(oopDesc* old_p) {
  // precond(is_alive(old_p));
  if (!is_alive(old_p)) {
    precond(!old_p->is_gc_marked() || is_dead_space(old_p));
    int size = old_p->size_given_klass(old_p->klass());
    return size;
  }

#ifdef ASSERT
  g_cntScan ++;
#endif

  oopDesc* new_anchor_p = NULL;
  if (!to_obj(old_p)->isTrackable()) {
    oopDesc* new_p = old_p->forwardee();
    if (new_p == NULL) new_p = old_p;
    if (!g_adjust_pointer_closure.is_in_young(new_p)) {
      to_obj(old_p)->markTrackable();
      if (to_obj(old_p)->isUnreachable()) {
        mark_survivor_reachable(old_p);
      }
      new_anchor_p = new_p;
    }
  } else {
    assert(!to_obj(old_p)->isUnreachable() || to_obj(old_p)->isUnstableMarked(), 
      "unreachable trackable %p(%s)\n", 
      old_p, getClassName(to_obj(old_p)));
  }
  postcond(rtHeap::is_alive(old_p));

  rtgc_log(LOG_OPT(8), "adjust_pointers %p->%p\n", old_p, new_anchor_p);
  g_adjust_pointer_closure.init(old_p, new_anchor_p);
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool is_young_root = g_adjust_pointer_closure._has_young_ref && to_obj(old_p)->isTrackable();
  if (is_young_root) {
    oopDesc* forwardee = old_p->forwardee();
    if (forwardee == NULL) forwardee = old_p;
    add_young_root(old_p, forwardee);
  }

  __adjust_anchor_pointers(old_p); 

  to_obj(old_p)->unmarkDirtyReferrerPoints();
  return size; 
}


void rtHeap::prepare_adjust_pointers(HeapWord* old_gen_heap_start) {
  g_adjust_pointer_closure._old_gen_start = old_gen_heap_start;
  rtgc_log(LOG_OPT(8), "old_gen_heap_start %p\n", old_gen_heap_start);
  // yg_root_locked = false;
  if (g_young_roots.size() > 0) {
    oop* src_0 = g_young_roots.adr_at(0);
    oop* dst = src_0;
    oop* end = src_0 + g_young_roots.size();
    for (oop* src = src_0; src < end; src++) {
      GCObject* node = to_obj(*src);
      if (node->isYoungRoot()) {
        node->unmarkYoungRoot();
      }
    }
    g_young_roots.resize(0);
  }
}

void GCNode::markGarbage(const char* reason)  {
  if (reason != NULL) {
    precond(this->isTrackable());
    rtgc_debug_log(this, "garbage marking on %p(%s) %s\n", this, getClassName(this), reason);
  }
  assert(!this->isGarbageMarked(),
      "already marked garbage %p(%s)\n", this, getClassName(this));
  assert(!cast_to_oop(this)->is_gc_marked() || reason == NULL,
      "invalid garbage marking on %p(%s) rc=%d discovered=%p ghost=%d\n", this, getClassName(this), this->getRootRefCount(),
      __get_discovered(cast_to_oop(this)), rtHeapEx::print_ghost_anchors((GCObject*)this));
  _flags.isGarbage = true;
  _flags.isPublished = true;
}

#ifdef ASSERT
bool rtHeapEx::print_ghost_anchors(GCObject* node, int depth) {
  if (depth > 0 && !rtHeap::is_alive(cast_to_oop(node))) return true;
  const int discovered_off = java_lang_ref_Reference::discovered_offset();
  AnchorIterator ai(node);
  if (!ai.hasNext()) {
    rtgc_log(1, "no anchors for %p\n", node);
  }
  while (ai.hasNext()) {
    if (node->hasSafeAnchor()) {
      GCObject* anchor = node->getSafeAnchor();
      bool isClass = cast_to_oop(anchor)->klass() == vmClasses::Class_klass();
      rtgc_log(1, "safe anchor[%d] %p(%s)[%d] unsafe:%d rc:%d gc_m:%d isClass=%d cldHolder=%p -> %p(%s) tr=%d\n", 
          depth, anchor, RTGC::getClassName(anchor),
          anchor->getShortcutId(), anchor->isUnstableMarked(), 
          anchor->getRootRefCount(), cast_to_oop(anchor)->is_gc_marked(), 
          isClass, !isClass ? NULL : (void*)cast_to_oop(anchor)->klass()->class_loader_data()->holder_no_keepalive(),
          node, RTGC::getClassName(node), node->isTrackable());
      print_ghost_anchors(anchor, depth + 1);
      return true;
    }
    GCObject* anchor = ai.next();
    if (!anchor->isGarbageMarked()) {//} && !is_java_reference(cast_to_oop(anchor), (ReferenceType)-1)) {
      bool isClass = cast_to_oop(anchor)->klass() == vmClasses::Class_klass();
      rtgc_log(1, "ghost anchor[%d] %p(%s)[%d] unsafe:%d rc:%d gc_m:%d isClass=%d cldHolder=%p -> %p(%s) tr=%d\n", 
          depth, anchor, RTGC::getClassName(anchor),
          anchor->getShortcutId(), anchor->isUnstableMarked(), 
          anchor->getRootRefCount(), cast_to_oop(anchor)->is_gc_marked(), 
          isClass, !isClass ? NULL : (void*)cast_to_oop(anchor)->klass()->class_loader_data()->holder_no_keepalive(),
          node, RTGC::getClassName(node), node->isTrackable());

      if (cast_to_oop(anchor)->is_gc_marked()) {
        cast_to_oop(anchor)->print_on(tty);
        return true;
      }

      if (depth < 5) {
        print_ghost_anchors(anchor, depth + 1);
      }
    }
  }
  return false;
}
#endif


void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
  if (is_alive(p) || (node->isTrackable() ? !node->isUnreachable() : node->hasReferrer())) {
    rtHeapEx::print_ghost_anchors(to_obj(p));
  }

  assert(!is_alive(p), "wrong on garbage %p[%d](%s) unreachable=%d tr=%d rc=%d hasRef=%d isUnsafe=%d\n", 
        node, node->getShortcutId(), RTGC::getClassName(node), node->isUnreachable(),
        node->isTrackable(), node->getRootRefCount(), node->hasReferrer(), node->isUnstableMarked());
  assert(node->isTrackable() || !node->hasReferrer(),
      "wrong on garbage %p[%d](%s) unreachable=%d tr=%d rc=%d hasRef=%d isUnsafe=%d ghost=%d\n", 
        node, node->getShortcutId(), RTGC::getClassName(node), node->isUnreachable(),
        node->isTrackable(), node->getRootRefCount(), node->hasReferrer(), node->isUnstableMarked(),
        rtHeapEx::print_ghost_anchors(to_obj(p)));
  precond(node->isTrackable() ? node->isUnreachable() : !node->hasReferrer());
  return;

  fatal("should not be here!!");
  rtgc_debug_log(p, "destroyed %p(ss)\n", node);//, RTGC::getClassName(node));
  rtgc_log(LOG_OPT(4), "destroyed %p(ss)\n", node);//, RTGC::getClassName(node));
  
#ifdef ASSERT
  if (ENABLE_GC) {  
    assert(node->getRootRefCount() == 0, "wrong refCount(%x) on garbage %p(%s)\n", 
        node->getRootRefCount(), node, RTGC::getClassName(node));
    // print_ghost_anchors(node);
  }
#endif

  rtgc_log(LOG_OPT(11), "trackable destroyed %p, yg-r=%d\n", node, node->isYoungRoot());

  node->markGarbage("destroy_trackable");
  node->removeAllAnchors();

  rtgc_log(LOG_OPT(4), "destroyed done %p(%s)\n", node, RTGC::getClassName(node));

  // rtgc_log(LOG_OPT(4), "destroyed done %p\n", node);
  if (!node->isTrackable()) {
    precond(!node->hasShortcut());
    return;
  }

  if (node->hasShortcut()) {
    SafeShortcut* ss = node->getShortcut();
    if (ss->tail() == node) {
      // rtgc_log(true, "garbage shortcut found [%d] %p\n", s_id, node);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      delete ss;
    }
  }
}

void rtHeap::finish_adjust_pointers() {
  g_adjust_pointer_closure._old_gen_start = NULL;
  rtHeapEx::adjust_ref_q_pointers(true);
  GCRuntime::adjustShortcutPoints();
  /**
   * adjust_pointers 수행 중에, mark_survivor_reachable() 이 호출된다.
   * 이에, rtHeap__clearStack() 이 adjust_pointers 종료 후에 호출되어야 한다.
   */
  rtHeap__clearStack<true>();
}

class ClearWeakHandleRef: public OopClosure {
  public:
  void do_object(oop* ptr) {
    oop v = *ptr;
    if (v != NULL) {
      rtHeap::clear_weak_reachable(v);
    }
  }
  virtual void do_oop(oop* o) { do_object(o); };
  virtual void do_oop(narrowOop* o) { fatal("It should not be here"); }
} clear_weak_handle_ref;

void rtHeap::prepare_rtgc(bool is_full_gc) {
  precond(g_stack_roots.size() == 0);
  if (is_full_gc) {
    // yg_root_locked = true;
    rtHeapEx::validate_trackable_refs();
    FreeMemStore::clearStore();
    if (RtLazyClearWeakHandle) {
      WeakProcessor::oops_do(&clear_weak_handle_ref);
    }
    // rtHeapEx::clear_finalizer_reachables();
    in_full_gc = true;
  } else {
    g_saved_young_root_count = g_young_roots.size();
  }
}

void rtHeap::finish_rtgc(bool is_full_gc) {
  if (!is_full_gc) {
    // link_pending_reference 수행 시, mark_survivor_reachable() 이 호출될 수 있다.
    rtHeap__clearStack<false>();
  }
  postcond(g_stack_roots.size() == 0);
  in_full_gc = false;
}


void rtHeap::lock_jni_handle(oopDesc* p) {
  if (!REF_LINK_ENABLED) return;
  rtgc_debug_log(p, "lock_handle %p\n", p);
  GCRuntime::onAssignRootVariable_internal(to_obj(p));
}

void rtHeap::release_jni_handle(oopDesc* p) {
  if (!REF_LINK_ENABLED) return;
  assert(to_obj(p)->isStrongRootReachable(), "wrong handle %p\n", p);
  rtgc_debug_log(p, "release_handle %p\n", p);
  GCRuntime::onEraseRootVariable_internal(to_obj(p));
}


void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.size(), full_gc); 
}

// int RecyclableGarbageArray::compare_obj_size(const void* left, const void* right) {
//   int left_size = obj_size(cast_to_oop(*(void**)left));
//   int right_size = obj_size(cast_to_oop(*(void**)right));
//   return left_size - right_size;
// }

void rtHeap::oop_recycled_iterate(RecycledTrackableClosure* closure) {
  for (int idx = g_resurrected_top; idx < g_stack_roots.size(); idx++) {
    GCObject* node = g_stack_roots.at(idx);
    if (!node->isTrackable()) {
      GCRuntime::onAssignRootVariable_internal(node);
      closure->do_iterate(cast_to_oop(node));
    }
    g_resurrected_top = INT_MAX; 
  }
}


void rtHeap__initialize() {
  g_young_roots.initialize();
  g_stack_roots.initialize();
}