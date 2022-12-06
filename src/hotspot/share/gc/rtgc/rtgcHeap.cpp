#include "runtime/globals.hpp"
#include "utilities/debug.hpp"
#include "oops/oop.inline.hpp"
#include "classfile/classLoaderDataGraph.hpp"
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

#include "gc/rtgc/rtgcGlobals.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

#include "rtHeapEx.hpp"
#include "rtCLDCleaner.hpp"
#include "rtThreadLocalData.hpp"

int rtHeap::in_full_gc = 0;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

using namespace RTGC;

namespace RTGC {
  // bool yg_root_locked = false;
  extern bool REF_LINK_ENABLED;
  bool ENABLE_GC = true && REF_LINK_ENABLED;
  bool g_in_gc_termination = false;
  bool is_gc_started = false;
  class RtAdjustPointerClosure: public BasicOopIterateClosure {
  public:
    template <typename T> void do_oop_work(T* p);
    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
    virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }

    bool is_in_young(void* p) { return p < _old_gen_start; }
    void init(oopDesc* old_anchor_p, oopDesc* new_anchor_p, bool is_trackable_forwardee) { 
      _old_anchor_p = old_anchor_p; 
      _new_anchor_p = new_anchor_p; 
      _has_young_ref = false; 
      _is_trackable_forwardee = is_trackable_forwardee;
    }
    bool _has_young_ref;
    HeapWord* _old_gen_start;
    bool _is_trackable_forwardee;
  private:
    oopDesc* _old_anchor_p;
    oopDesc* _new_anchor_p;    
  };

  
  bool g_in_iterate_younger_gen_roots = false;
  HugeArray<oop> g_young_roots;
  HugeArray<GCObject*> g_stack_roots;
  HugeArray<GCObject*> g_resurrected;
  Thread* gcThread = NULL;
  int g_cntTrackable = 0;
  int g_cntScan = 0;
  int g_saved_young_root_count = 0;
  RtAdjustPointerClosure g_adjust_pointer_closure;
  RtYoungRootClosure* g_young_root_closure;
  const bool USE_PENDING_TRACKABLES = false;
  oopDesc* empty_trackable;
}

namespace RTGC_unused {
  bool IS_GC_MARKED(oopDesc* obj) {
    return obj->is_gc_marked();
  }

  bool is_java_reference(oopDesc* obj, ReferenceType rt) {
    return obj->klass()->id() == InstanceRefKlassID && 
          (rt == (ReferenceType)-1 || ((InstanceRefKlass*)obj->klass())->reference_type() == rt);
  }

};

using namespace RTGC;

static oopDesc* __get_discovered(oop obj) {
  return obj->klass()->id() != InstanceRefKlassID ? NULL
    : java_lang_ref_Reference::discovered(obj);
}


bool rtHeap::is_alive(oopDesc* p, bool must_not_destroyed) {
  GCObject* node = to_obj(p);
  if (!must_not_destroyed) {
    return !node->isGarbageMarked() && (node->isTrackable() || p->is_gc_marked());
  }
  else {
    if (node->isTrackable()) {
      return !node->isGarbageMarked();
    } else {
      assert(!node->isGarbageMarked(), "destroyed object %p(%s)\n", p, RTGC::getClassName(p));
      return p->is_gc_marked();
    }
  }
}

bool rtHeap::is_destroyed(oopDesc* p) {
  return to_obj(p)->isDestroyed();
}

void rtHeap::add_young_root(oopDesc* old_p, oopDesc* new_p) {
  GCObject* node = to_obj(old_p);
  precond(node->isTrackable());
  precond(!node->isGarbageMarked());
  // precond(g_young_roots.indexOf(old_p) < 0);
  // precond(g_young_roots.indexOf(new_p) < 0);
  node->markYoungRoot();
  g_young_roots.push_back(new_p);
  rtgc_debug_log(old_p, "mark YG Root (%p)->%p idx=%d\n", old_p, new_p, g_young_roots.size());
}

bool rtHeap::is_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isTrackable();
}

void rtHeap::lock_jni_handle_at_safepoint(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onAssignRootVariable_internal(obj);
}

void rtHeap::release_jni_handle_at_safepoint(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onEraseRootVariable_internal(obj);
}

static bool is_adjusted_trackable(oopDesc* new_p) {
  if (to_obj(new_p)->isTrackable()) return true;
  return !USE_EXPLICIT_TRACKABLE_MARK && 
      rtHeap::in_full_gc && 
      to_obj(new_p->forwardee())->isTrackable();
}

void rtHeap::mark_promoted_trackable(oopDesc* new_p) {
  // YG GC 수행 도중에 old-g로 옮겨진 객체들을 marking 한다.
  if (USE_EXPLICIT_TRACKABLE_MARK) {
    precond(!to_obj(new_p)->isTrackable());
    to_obj(new_p)->markTrackable();
  } else {
    precond(is_adjusted_trackable(new_p));
  }
  rtgc_debug_log(new_p, "mark_promoted_trackable %p\n", new_p);
  rtCLDCleaner::lock_cld(new_p);
}

void rtHeap::mark_tenured_trackable(oopDesc* new_p) {
  // YG GC 수행 전에, old-heap 에 allocate 된 객체들을 marking 한다.
  precond (!to_obj(new_p)->isTrackable());
  mark_promoted_trackable(new_p);
  GCRuntime::detectUnsafeObject(to_obj(new_p));
}

void rtHeapUtil::resurrect_young_root(GCObject* node) {
  // precond(g_in_iterate_younger_gen_roots);
  precond(node->isGarbageMarked());
  precond(node->isTrackable());
  if (!rtHeap::in_full_gc) {
    precond(node->isYoungRoot() && node->isDirtyReferrerPoints());
  }
  node->unmarkGarbage();
  node->unmarkDirtyReferrerPoints();  
  oop anchor = g_young_root_closure->current_anchor();
  precond(rtHeap::in_full_gc || anchor == NULL);
  if (false && anchor != NULL) {
    precond(!node->hasSafeAnchor());
    node->setSafeAnchor(to_obj(anchor));
    node->setShortcutId_unsafe(INVALID_SHORTCUT);
  } else {
    node->invalidateSafeAnchor();
  }
  rtgc_log(LOG_OPT(11), "resurrect obj %p(%s) -> %p(%s YR=%d)\n", 
      (void*)anchor, getClassName(anchor), node, getClassName(node), node->isYoungRoot());
  if (!g_young_root_closure->iterate_tenured_young_root_oop(cast_to_oop(node))) {
    if (node->isYoungRoot()) {
      node->unmarkYoungRoot();
    }
  } else if (!node->isYoungRoot()) {
    rtHeap::add_young_root(cast_to_oop(node), cast_to_oop(node));
  }
  if (rtHeap::in_full_gc) {
    rtCLDCleaner::resurrect_cld(cast_to_oop(node));
  }
}

void rtHeap__addRootStack_unsafe(GCObject* node) {
  g_stack_roots.push_back(node);
}

void rtHeap__addUntrackedTenuredObject(GCObject* node, bool is_recycled) {
  precond(!is_gc_started || !rtHeap::in_full_gc);
  if (!is_gc_started || is_recycled) {
    rtgc_debug_log(node, "rtHeap__addUntrackedTenuredObject %p, recycled=%d \n", node, is_recycled);
    g_resurrected.push_back(node);
  }
}

void rtHeap__processUntrackedTenuredObjects() {
  for (int idx = 0; idx < g_resurrected.size(); idx++) {
    GCObject* node = g_resurrected.at(idx);
    rtgc_debug_log(node, "rtHeap__processUntrackedTenuredObjects %d: %p\n", idx, node);
    rtHeap::mark_promoted_trackable(cast_to_oop(node));
    GCRuntime::detectUnsafeObject(node);
  }
  if (!USE_EXPLICIT_TRACKABLE_MARK) {
    g_resurrected.resize(0); 
  }
}


void rtHeap::mark_survivor_reachable(oopDesc* new_p) {
  GCObject* node = to_obj(new_p);
  assert(is_adjusted_trackable(new_p), "must be trackable\n" PTR_DBG_SIG, PTR_DBG_INFO(new_p));
  if (node->isGarbageMarked()) {
    assert(node->isTrackable(), "not yr " PTR_DBG_SIG, PTR_DBG_INFO(node));
    rtHeapUtil::resurrect_young_root(node);
    if (node->hasSafeAnchor()) return;
    // garbage marking 된 상태는 stack marking 이 끝난 상태.
  }
  if (!node->isSurvivorReachable()) {
    rtgc_log(LOG_OPT(9), "add stack root %p\n", new_p);
    node->markSurvivorReachable();
    //GCRuntime::onAssignRootVariable_internal(node);
    g_stack_roots.push_back(node);
  }
}


template<bool is_full_gc>
void rtHeap__clearStack() {
  rtHeapEx::g_lock_unsafe_list = false;
  int cnt_root = g_stack_roots.size();
  if (cnt_root > 0) {
    GCObject** src = &g_stack_roots.at(0);
    GCObject** end = src + cnt_root;
    rtgc_log(LOG_OPT(8), "clear_stack_roots %d\n", 
        g_stack_roots.size());

    for (; src < end; src++) {
      GCObject* erased = src[0];
      precond(erased->isTrackable());
      assert(erased->isSurvivorReachable(), "%p rc=%x\n", erased, erased->getRootRefCount());
      if (erased->unmarkSurvivorReachable() <= ZERO_ROOT_REF) {
        if (!erased->hasSafeAnchor() && !erased->isUnstableMarked()) {
          erased->markUnstable();
          if (is_full_gc) {
            oop new_p = cast_to_oop(erased)->forwardee();
            erased = new_p == NULL ? erased : to_obj(new_p);
          }
          _rtgc.g_pGarbageProcessor->addUnstable_ex(erased);
        }
      }
    }

    rtgc_log(LOG_OPT(8), "iterate_stack_roots done %d\n", g_stack_roots.size());
    g_stack_roots.resize(0);
  }
}


void rtHeap__clear_garbage_young_roots(bool is_full_gc) {
  if (!is_full_gc) {
    _rtgc.g_pGarbageProcessor->validateGarbageList();
  }
  _rtgc.g_pGarbageProcessor->collectGarbage(is_full_gc);

  if (!is_full_gc) {
    int old_cnt = g_young_roots.size();
    for (int i = old_cnt; --i >= 0; ) {
      GCObject* node = to_obj(g_young_roots.at(i));
      if (node->isGarbageMarked() || !node->isYoungRoot()) {
        g_young_roots.removeFast(i);
      } else if (is_full_gc && rtHeap::DoCrossCheck) {
        precond(cast_to_oop(node)->is_gc_marked());
      }
    }
    rtgc_log(LOG_OPT(8), "rtHeap__clear_garbage_young_roots done %d->%d garbages=%d\n", 
        old_cnt, g_young_roots.size(), _rtgc.g_pGarbageProcessor->getGarbageNodes()->size());

    rtHeap__clearStack<false>();

    rtHeapEx::g_lock_garbage_list = true;
  } else {
    // g_in_iterate_younger_gen_roots = true;
    // rtCLDCleaner::clear_cld_locks(g_young_root_closure);
    // rtCLDCleaner::collect_garbage_clds(g_young_root_closure);
    // _rtgc.g_pGarbageProcessor->collectGarbage(is_full_gc);
    // g_in_iterate_younger_gen_roots = false;    
  }
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
  rtHeapEx::g_lock_garbage_list = false;

#ifdef ASSERT
  for (int idx_root = young_root_count; --idx_root >= 0; ) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
    assert(!node->isGarbageMarked(), "invalid yg-root %p(%s)\n", node, RTGC::getClassName(node));
  }
#endif

  HugeArray<GCObject*>* garbages = _rtgc.g_pGarbageProcessor->getGarbageNodes(); 
  assert(garbages->size() == 0, "garbages->size %d\n", garbages->size());

  g_in_iterate_younger_gen_roots = true;
  for (int idx_root = young_root_count; --idx_root >= 0; ) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
    if (node->isGarbageMarked()) {
      // ref-pending-list 를 추가하는 과정에서, yg-root 에서 제거된 객체가 
      // 다시 추가될 수 있다.
      precond(is_full_gc || node->isDirtyReferrerPoints());
      continue;
    }
    assert(is_full_gc || node->isYoungRoot(), "invalid young root %p(%s)\n", node, RTGC::getClassName(node));
    if (is_full_gc) {
      if (!node->isYoungRoot()) {
        // detectGarbage 에 의해 garbageMarking 된 후, 
        // closure->iterate_tenured_young_root_oop() 처리 시 young_root 가 해제될 수 있다.
        g_young_roots.removeFast(idx_root);
        continue;
      }
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

  if (is_full_gc) {
    rtCLDCleaner::clear_cld_locks(g_young_root_closure);
    rtCLDCleaner::collect_garbage_clds(g_young_root_closure);
  }
  g_in_iterate_younger_gen_roots = false;

}


void rtHeap::add_trackable_link(oopDesc* anchor, oopDesc* link) {
  if (anchor == link) return;
  GCObject* node = to_obj(link);
  assert(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)\n", anchor, anchor->klass()->name()->bytes());
  if (node->isGarbageMarked()) {
    assert(node->isTrackable(), "must trackable %p(%s)\n", node, RTGC::getClassName(node));

    assert(node->isAnchored() || (node->isYoungRoot() && node->isDirtyReferrerPoints()), 
        "invalid link %p(%s) -> %p(%s)\n", 
        anchor, RTGC::getClassName(to_obj(anchor)), node, RTGC::getClassName(node));

    assert(in_full_gc || node->isYoungRoot(), "no y-root %p(%s)\n",
        node, RTGC::getClassName(node));
    rtHeapUtil::resurrect_young_root(node);
  }

  precond(to_obj(anchor)->isTrackable() && !to_obj(anchor)->isGarbageMarked());
  RTGC::add_referrer_ex(link, anchor, false);
}


void rtHeap::mark_forwarded(oopDesc* p) {
  GCObject* node = to_obj(p);
  precond(!node->isGarbageMarked());
  
  assert(!node->isTrackable() || // unreachble 상태가 아니어야 한다.
    node->isStrongRootReachable() || node->isAnchored() || node->isUnstableMarked(),
      " invalid node " PTR_DBG_SIG, PTR_DBG_INFO(node));
  // TODO markDirty 시점이 너무 이름. 필요없다??
  node->markDirtyReferrerPoints();
}


template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  assert(!rtHeapEx::OptStoreOop || sizeof(T) == sizeof(oop) || 
      _new_anchor_p != NULL || !_is_trackable_forwardee ||
      (void*)CompressedOops::decode(*p) == _old_anchor_p || !rtHeap::is_modified(*p), 
      "modified field [%d] v = %x(%s)\n" PTR_DBG_SIG, 
      (int)((address)p - (address)_old_anchor_p), *(int32_t*)p, 
      RTGC::getClassName(CompressedOops::decode(*p)),
      PTR_DBG_INFO(_old_anchor_p));

  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  if (rtHeapEx::OptStoreOop && _is_trackable_forwardee) {
    *p = rtHeap::to_unmodified(*p);
  }
  if (old_p == NULL || old_p == _old_anchor_p) return;

#ifdef ASSERT
  rtHeapUtil::ensure_alive_or_deadsapce(old_p, _old_anchor_p);
#endif   

  _has_young_ref |= is_in_young(new_p);
  if (_new_anchor_p == NULL) {
    return;
  }

  // old_p 내부 field 에 대한 adjust_pointers 가 처리되지 않았으면...
  if (to_obj(old_p)->isDirtyReferrerPoints()) {
    // old_p 에 대해 adjust_pointers 를 수행하기 전.
    RTGC::add_referrer_unsafe(old_p, _old_anchor_p, _old_anchor_p);
  }
  else {
    RTGC::add_referrer_unsafe(old_p, _new_anchor_p, _old_anchor_p);
  }
}


static void adjust_anchor_pointer(ShortOOP* p, GCObject* node) {
  GCObject* old_p = p[0];
  precond(!old_p->isGarbageMarked());
  GCObject* new_obj = to_obj(cast_to_oop(old_p)->forwardee());
  if (new_obj != NULL) {
    rtgc_log(LOG_OPT(11), "anchor moved %p->%p in %p\n", old_p, new_obj, node);
    p[0] = new_obj;
  }  
}

size_t rtHeap::adjust_pointers(oopDesc* old_p) {
  if (!is_alive(old_p, false)) {
    precond(!old_p->is_gc_marked() || rtHeapUtil::is_dead_space(old_p));
    int size = old_p->size_given_klass(old_p->klass());
    return size;
  }

#ifdef ASSERT
  g_cntScan ++;
#endif

  GCObject* node = to_obj(old_p);
  oopDesc* new_anchor_p = NULL;
  bool is_trackable_forwardee = node->isTrackable();
  if (!is_trackable_forwardee) {
    oopDesc* new_p = old_p->forwardee();
    if (new_p == NULL) new_p = old_p;
    if (!g_adjust_pointer_closure.is_in_young(new_p)) {
      is_trackable_forwardee = true;
      mark_promoted_trackable(old_p);
      if (node->isUnreachable()) {
        // GC 종료 후 UnsafeList 에 추가.
        mark_survivor_reachable(old_p);
      }
      new_anchor_p = new_p;
    }
  } else {
    precond(old_p->forwardee() == NULL || !g_adjust_pointer_closure.is_in_young(old_p->forwardee()));
    // ensure UnsafeList is empty.
    assert(!node->isUnreachable() || node->isUnstableMarked(), 
      "unreachable trackable %p(%s)\n", 
      old_p, getClassName(node));
  }

  rtgc_log(LOG_OPT(8), "adjust_pointers %p->%p\n", old_p, new_anchor_p);
  g_adjust_pointer_closure.init(old_p, new_anchor_p, is_trackable_forwardee);
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool is_young_root = g_adjust_pointer_closure._has_young_ref && node->isTrackable();
  if (is_young_root) {
    oopDesc* forwardee = old_p->forwardee();
    if (forwardee == NULL) forwardee = old_p;
    add_young_root(old_p, forwardee);
  }

  if (node->isAnchored()) {
    if (node->hasMultiRef()) {
      ReferrerList* referrers = node->getReferrerList();
      assert(!referrers->isTooSmall(), "invalid anchorList " PTR_DBG_SIG, PTR_DBG_INFO(node));
      for (ReverseIterator it(referrers); it.hasNext(); ) {
        ShortOOP* ptr = (ShortOOP*)it.getAndNext();
        adjust_anchor_pointer(ptr, node);
      }
    }
    else {
      adjust_anchor_pointer((ShortOOP*)&node->_refs, node);
    }
  }

  node->unmarkDirtyReferrerPoints();
  return size; 
}


void rtHeap::prepare_adjust_pointers(HeapWord* old_gen_heap_start) {
  g_adjust_pointer_closure._old_gen_start = old_gen_heap_start;
  rtgc_log(LOG_OPT(2), "old_gen_heap_start %p\n", old_gen_heap_start);
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
  assert(this->getRootRefCount() <= (reason != NULL ? ZERO_ROOT_REF : ZERO_ROOT_REF + 1),
      "invalid garbage marking on %p(%s) rc=%d\n", this, getClassName(this), this->getRootRefCount());
  assert(!cast_to_oop(this)->is_gc_marked() || reason == NULL,
      "invalid garbage marking on %p(%s) rc=%d discovered=%p ghost=%d\n", this, getClassName(this), this->getRootRefCount(),
      __get_discovered(cast_to_oop(this)), rtHeapEx::print_ghost_anchors((GCObject*)this));
  _flags.isGarbage = true;
  _flags.isPublished = true;
}

#ifdef ASSERT
bool rtHeapEx::print_ghost_anchors(GCObject* node, int depth) {
  if (depth > 0 && !rtHeap::is_alive(cast_to_oop(node)), false) return true;
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
  if (is_alive(p, false) || (node->isTrackable() ? !node->isUnreachable() : node->isAnchored())) {
    rtHeapEx::print_ghost_anchors(to_obj(p));
  }

  assert(is_destroyed(p), "wrong on garbage %p[%d](%s) unreachable=%d tr=%d rc=%d hasRef=%d isUnsafe=%d ghost=%d\n", 
        node, node->getShortcutId(), RTGC::getClassName(node), node->isUnreachable(),
        node->isTrackable(), node->getRootRefCount(), node->isAnchored(), node->isUnstableMarked(),
        rtHeapEx::print_ghost_anchors(to_obj(p)));
  precond(node->isTrackable() ? node->isUnreachable() : !node->isAnchored());
  return;
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
      rtHeap::release_jni_handle_at_safepoint(v);
    }
  }
  virtual void do_oop(oop* o) { do_object(o); };
  virtual void do_oop(narrowOop* o) { fatal("It should not be here"); }
} clear_weak_handle_ref;

void rtHeap::prepare_rtgc(ReferencePolicy* policy) {
  rtgc_log(LOG_OPT(1), "prepare_rtgc %p\n", policy);
  if (policy == NULL) {
    is_gc_started = true;
    rtHeap__processUntrackedTenuredObjects();
    precond(g_stack_roots.size() == 0);
    if (rtHeapEx::OptStoreOop) {
      UpdateLogBuffer::process_update_logs();
    }
    g_saved_young_root_count = g_young_roots.size();
  } else {
    // yg_root_locked = true;
    rtHeapEx::validate_trackable_refs();
    FreeMemStore::clearStore();
    if (RtLazyClearWeakHandle) {
      WeakProcessor::oops_do(&clear_weak_handle_ref);
    }
    in_full_gc = 1;
    rtHeapEx::break_reference_links(policy);
  }
}


void rtHeap::finish_rtgc(bool is_full_gc_unused, bool promotion_finished_unused) {
  precond(GCNode::g_trackable_heap_start == GenCollectedHeap::heap()->old_gen()->reserved().start());
  rtgc_log(LOG_OPT(1), "finish_rtgc full_gc=%d\n", in_full_gc);
  is_gc_started = false;
  if (!in_full_gc) {
    // link_pending_reference 수행 시, mark_survivor_reachable() 이 호출될 수 있다.
    rtHeap__clearStack<false>();
  }
  if (rtHeapEx::OptStoreOop) {
    UpdateLogBuffer::reset_gc_context();
  }
  rtHeapEx::g_lock_unsafe_list = false;
  postcond(g_stack_roots.size() == 0);
  in_full_gc = 0;
  g_in_gc_termination = true;
}


// void rtHeap::lock_jni_handle(oopDesc* p) {
//   if (!REF_LINK_ENABLED) return;
//   rtgc_debug_log(p, "lock_handle %p\n", p);
//   GCRuntime::onAssignRootVariable(to_obj(p));
// }

// void rtHeap::release_jni_handle(oopDesc* p) {
//   if (!REF_LINK_ENABLED) return;
//   assert(to_obj(p)->isStrongRootReachable(), "wrong handle %p(%s) isClass=%d\n", p, RTGC::getClassName(to_obj(p)), p->klass() == vmClasses::Class_klass());
//   rtgc_debug_log(p, "release_handle %p\n", p);
//   GCRuntime::onEraseRootVariable_internal(to_obj(p));
// }


void rtHeap::print_heap_after_gc(bool full_gc) {  
  g_in_gc_termination = false;
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.size(), full_gc); 
}

// copyed from CollectedHeap.cpp
size_t CollectedHeap::filler_array_hdr_size() {
  return align_object_offset(arrayOopDesc::header_size(T_INT)); // align to Long
}

size_t CollectedHeap::filler_array_min_size() {
  return align_object_size(filler_array_hdr_size()); // align to MinObjAlignment
}

void rtgc_fill_dead_space(HeapWord* start, HeapWord* end, bool zap) {
  GCObject* obj = to_obj(start);
  // precond(obj->isTrackable());

  oopDesc::clear_rt_node(start);
  obj->markGarbage(NULL);
  obj->markDestroyed();

  // size_t words = pointer_delta(end, start);
  // if (words >= CollectedHeap::filler_array_min_size()) {
  //   fill_with_array(start, words, zap);
  // } else if (words > 0) {
  //     init_dead_space(start, vmClasses::Object_klass(), -1);
  // }
  //RuntimeHeap::reclaimObject(obj);
  CollectedHeap::fill_with_object(start, end, zap);
}

void rtHeap__ensure_trackable_link(oopDesc* anchor, oopDesc* obj) {
  if (anchor != obj) {
    assert(rtHeap::is_alive(obj), "must not a garbage \n" PTR_DBG_SIG, PTR_DBG_INFO(obj)); 
    assert(to_obj(obj)->hasReferrer(to_obj(anchor)), 
        "invalid link %d\n anchor=" PTR_DBG_SIG "link=" PTR_DBG_SIG,
        rtHeapEx::print_ghost_anchors(to_obj(obj)), PTR_DBG_INFO(anchor), PTR_DBG_INFO(obj)); 
  }
}


void rtHeap::oop_recycled_iterate(ObjectClosure* closure) {
  if (USE_EXPLICIT_TRACKABLE_MARK) {
    // fatal("full gc 도 oop_recycled_iterate() 호출해야 한다");
    // precond(is_gc_started && !in_full_gc);
    for (int idx = 0; idx < g_resurrected.size(); idx++) {
      GCObject* node = g_resurrected.at(idx);
      // rtgc_debug_log(node, "oop_recycled_iterate %p\n", node);
      // rtgc_log(true, "oop_recycled_iterate %p\n", node);
      closure->do_object(cast_to_oop(node));
    }
    g_resurrected.resize(0); 
  }
}

int cnt_init = 0;
void rtHeap__initialize() {
  rtgc_log(true, "trackable_heap_start = %p narrowKalssOpp:base = %p klass_offset_in_bytes=%d\n", 
    GCNode::g_trackable_heap_start, 
    CompressedKlassPointers::base(),
    oopDesc::klass_offset_in_bytes());

  g_young_roots.initialize();
  g_stack_roots.initialize();
  g_resurrected.initialize();
  rtCLDCleaner::initialize();
  UpdateLogBuffer::reset_gc_context();

}

