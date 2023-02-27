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
#include "gc/serial/serialHeap.inline.hpp"


int rtHeap::in_full_gc = 0;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

using namespace RTGC;
Stack<oop, mtGC> MarkSweep::_resurrect_stack;

namespace RTGC {
  // bool yg_root_locked = false;
  bool g_in_progress_marking = false;
  int cnt_resurrect = 0;

  extern bool REF_LINK_ENABLED;
  bool ENABLE_GC = true && REF_LINK_ENABLED;
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
    bool _trackable_old_anchor;
  private:
    oopDesc* _old_anchor_p;
    oopDesc* _new_anchor_p;    
  };

  
  HugeArray<oop> g_young_roots;
  HugeArray<GCObject*> g_stack_roots;
  HugeArray<GCObject*> g_recycled;
  Thread* gcThread = NULL;
#ifdef ASSERT  
  int g_debug_cnt_untracked = 0;
#endif
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

void rtHeap::init_mark(oopDesc* p) {
  if (UseBiasedLocking) {  
    p->set_mark(markWord::prototype_for_klass(p->klass()));
  } else {
    p->set_mark(markWord::prototype());
  }
}

bool rtHeap::is_alive(oopDesc* p, bool must_not_destroyed) {
  GCObject* node = to_obj(p);
  if (!must_not_destroyed) {
    return !node->isGarbageMarked() && (node->isTrackable_unsafe() || p->is_gc_marked());
  }
  else {
    if (node->isTrackable_unsafe()) {
      return !node->isGarbageMarked();
    } else {
      rt_assert_f(!node->isGarbageMarked(), "destroyed object %p(%s)", p, RTGC::getClassName(p));
      return p->is_gc_marked();
    }
  }
}

bool rtHeap::is_destroyed(oopDesc* p) {
  return to_obj(p)->isDestroyed();
}

void rtHeap::add_young_root(oopDesc* old_p, oopDesc* new_p) {
  GCObject* node = to_obj(old_p);
  rt_assert(node->is_adjusted_trackable());
  rt_assert(!node->isGarbageMarked());
  // rt_assert(g_young_roots.indexOf(old_p) < 0);
  // rt_assert(g_young_roots.indexOf(new_p) < 0);
  node->markYoungRoot();
  g_young_roots.push_back(new_p);
  rtgc_log(LOG_OPT(7), "mark YG Root (%p)->%p idx=%d", old_p, new_p, g_young_roots.size());
  rtgc_debug_log(old_p, "mark YG Root (%p)->%p idx=%d", old_p, new_p, g_young_roots.size());
}

bool rtHeap::is_trackable(oopDesc* p) {
  if (!EnableRTGC) return false;
  GCObject* obj = to_obj(p);
  bool isTrackable = obj->isTrackable_unsafe();
  return isTrackable;
}

void rtHeap::lock_jni_handle_at_safepoint(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onAssignRootVariable_internal(obj);
}

void rtHeap::release_jni_handle_at_safepoint(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onEraseRootVariable_internal(obj);
}

// static bool is_adjusted_trackable(oopDesc* new_p) {
//   if (to_obj(new_p)->isTrackable()) return true;
//   return AUTO_TRACKABLE_MARK_BY_ADDRESS && 
//       rtHeap::in_full_gc && 
//       to_obj(new_p->forwardee())->isTrackable();
// }

void rtHeap::mark_promoted_trackable(oopDesc* new_p) {
  // GC 수행 도중에 old-g로 옮겨진 객체들을 marking 한다.
  GCObject* node = to_obj(new_p);
  if (!AUTO_TRACKABLE_MARK_BY_ADDRESS) {
    rt_assert(!node->isTrackable());
    node->markTrackable();
  } else {
    rt_assert(node->is_adjusted_trackable());
  }
  rtgc_debug_log(new_p, "mark_promoted_trackable %p->%p", new_p, (void*)new_p->forwardee());
  rtCLDCleaner::lock_cld(new_p);
}


void rtHeap::mark_young_root(oopDesc* tenured_p, bool is_young_root) {
  GCObject* node = to_obj(tenured_p);
  if (!is_young_root) {
    if (node->isYoungRoot()) {
      node->unmarkYoungRoot();
    }
  } else if (!node->isYoungRoot()) {
    rtHeap::add_young_root(tenured_p, tenured_p);
  }

}

void rtHeapUtil::resurrect_young_root(GCObject* node) {
  rtgc_log((++cnt_resurrect % 100) == 0, "resurrect_young_root cnt=%d %p", cnt_resurrect, node);
  rt_assert(node->isGarbageMarked());
  rt_assert(node->isTrackable());
  if (!rtHeap::in_full_gc) {
    rt_assert(node->isYoungRoot() && node->isDirtyReferrerPoints());
  }
  node->unmarkGarbage();
  node->unmarkDirtyReferrerPoints();  
  node->getMutableNode()->invalidateSafeAnchor();
  MarkSweep::_resurrect_stack.push(cast_to_oop(node));
  if (rtHeap::in_full_gc) {
    rtCLDCleaner::resurrect_cld(cast_to_oop(node));
  }
}

void rtHeap__addRootStack_unsafe(GCObject* node) {
  rt_assert(node->isTrackable());
  g_stack_roots.push_back(node);
}

void rtHeap__addUntrackedTenuredObject(GCObject* node, bool is_recycled) {
  rt_assert(!is_gc_started || !rtHeap::in_full_gc);
  if (!is_gc_started || is_recycled) {
    g_recycled.push_back(node);
  }
}

void rtHeap__processUntrackedTenuredObjects() {
  for (int idx = 0; idx < g_recycled.size(); idx++) {
    GCObject* node = g_recycled.at(idx);
    rtgc_debug_log(node, "rtHeap__processUntrackedTenuredObjects %d: %p", idx, node);
    rtHeap::mark_promoted_trackable(cast_to_oop(node));
    GCRuntime::detectUnsafeObject(node);
  }
#ifdef ASSERT
  g_debug_cnt_untracked = g_recycled.size();
#endif
  if (AUTO_TRACKABLE_MARK_BY_ADDRESS) {
    g_recycled.resize(0); 
  }
}


void rtHeap::mark_survivor_reachable(oopDesc* new_p) {
  rt_assert(EnableRTGC);
  GCObject* node = to_obj(new_p);
  rt_assert_f(node->is_adjusted_trackable(), "must be trackable\n" PTR_DBG_SIG, PTR_DBG_INFO(new_p));
  if (node->isGarbageMarked()) {
    rt_assert_f(node->isTrackable(), "not yr " PTR_DBG_SIG, PTR_DBG_INFO(node));
    rtHeapUtil::resurrect_young_root(node);
    if (node->node_()->hasSafeAnchor()) return;
    // garbage marking 된 상태는 stack marking 이 끝난 상태.
  }

  if (!node->isSurvivorReachable()) {
    rtgc_log(LOG_OPT(9), "add stack root %p", new_p);
    node->markSurvivorReachable_unsafe();
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
    rtgc_log(LOG_OPT(1), "clear_stack_roots %d", 
        g_stack_roots.size());

    for (; src < end; src++) {
      GCObject* erased = src[0];
      rt_assert(erased->is_adjusted_trackable());
      rt_assert_f(erased->isSurvivorReachable(), "%p rc=%x", erased, erased->getRootRefCount());
      if (erased->unmarkSurvivorReachable() <= ZERO_ROOT_REF) {
        if (!erased->node_()->hasSafeAnchor() && !erased->isUnstableMarked()) {
          erased->markUnstable();
          if (RTGC_FAT_OOP && is_full_gc) {
            oop new_p = cast_to_oop(erased)->forwardee();
            erased = new_p == NULL ? erased : to_obj(new_p);
          }
          _rtgc.g_pGarbageProcessor->addUnstable_ex(erased);
        }
      }
    }

    rtgc_log(LOG_OPT(1), "iterate_stack_roots done %d", g_stack_roots.size());
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
        rt_assert(cast_to_oop(node)->is_gc_marked());
      }
    }
    rtgc_log(LOG_OPT(1), "rtHeap__clear_garbage_young_roots done %d->%d garbages=%d", 
        old_cnt, g_young_roots.size(), _rtgc.g_pGarbageProcessor->getGarbageNodes()->size());

    rtHeap__clearStack<false>();

    rtHeapEx::g_lock_garbage_list = true;
  } else {
    // rtCLDCleaner::clear_cld_locks(g_young_root_closure);
    // rtCLDCleaner::collect_garbage_clds(g_young_root_closure);
    // _rtgc.g_pGarbageProcessor->collectGarbage(is_full_gc);
  }
}

void rtHeap::iterate_younger_gen_roots(RtYoungRootClosure* closure, bool is_full_gc) {
  g_young_root_closure = closure;
  int young_root_count = is_full_gc ? g_young_roots.size() : g_saved_young_root_count;
  rtHeapEx::g_lock_garbage_list = false;

#ifdef ASSERT
  for (int idx_root = young_root_count; --idx_root >= 0; ) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
    rt_assert_f(!node->isGarbageMarked(), "invalid yg-root %p(%s)", node, RTGC::getClassName(node));
  }
#endif

  HugeArray<GCObject*>* garbages = _rtgc.g_pGarbageProcessor->getGarbageNodes(); 
  rt_assert_f(garbages->size() == 0, "garbages->size %d", garbages->size());

  for (int idx_root = young_root_count; --idx_root >= 0; ) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
    if (node->isGarbageMarked()) {
      // ref-pending-list 를 추가하는 과정에서, yg-root 에서 제거된 객체가 
      // 다시 추가될 수 있다.
      rt_assert(is_full_gc || node->isDirtyReferrerPoints());
      continue;
    }
    rt_assert_f(is_full_gc || node->isYoungRoot(), "invalid young root %p(%s)", node, RTGC::getClassName(node));
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
      // rtgc_log(LOG_OPT(7), "skip garbage yg-root %p", node);
      continue;
    }

    // rtgc_log(LOG_OPT(7), "iterate yg root %p", (void*)node); 0x3f74adc18
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
}

void rtHeap::mark_resurrected_link(oopDesc* anchor, oopDesc* link) {
  GCObject* node = to_obj(link);
  rt_assert(node->isTrackable());
  if (anchor == link) return;
  rt_assert_f(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)", anchor, anchor->klass()->name()->bytes());
  rt_assert(node->hasReferrer(to_obj(anchor)));

  if (node->isGarbageMarked()) {
    rt_assert_f(node->node_()->hasAnchor() || (node->isYoungRoot() && node->isDirtyReferrerPoints()), 
        "invalid link %p(%s) -> %p(%s)", 
        anchor, RTGC::getClassName(to_obj(anchor)), node, RTGC::getClassName(node));
    rtHeapUtil::resurrect_young_root(node);
    node->getMutableNode()->setSafeAnchor(to_obj(anchor));
  }
}

void rtHeap::add_trackable_link(oopDesc* anchor, oopDesc* link) {
  if (anchor == link) return;
  GCObject* node = to_obj(link);
  rt_assert_f(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)", anchor, anchor->klass()->name()->bytes());
  if (node->isGarbageMarked()) {
    rt_assert_f(node->node_()->hasAnchor() || (node->isYoungRoot() && node->isDirtyReferrerPoints()), 
        "invalid link %p(%s) -> %p(%s)", 
        anchor, RTGC::getClassName(to_obj(anchor)), node, RTGC::getClassName(node));

    rt_assert_f(in_full_gc || node->isYoungRoot(), "no y-root %p(%s)",
        node, RTGC::getClassName(node));
    rtHeapUtil::resurrect_young_root(node);
  }

  // rtgc_debug_log(link, "trackable_barrier anchor %p link: %p", anchor, link);

  rt_assert(to_obj(anchor)->isTrackable() && !to_obj(anchor)->isGarbageMarked());
  RTGC::add_referrer_ex(link, anchor, false);
}


void rtHeap::mark_forwarded_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
  rt_assert(!node->isGarbageMarked());  
  if (node->isTrackable_unsafe()) {
    rt_assert_f(!node->isUnreachable(), PTR_DBG_SIG, PTR_DBG_INFO(node));
    markWord mark = p->mark();
    if (p->mark_must_be_preserved(mark)) {
      MarkSweep::preserve_mark(p, mark);
    }
  } else {
    rt_assert(p->is_gc_marked());
  }
  // TODO markDirty 시점이 너무 이름. 필요없다??
  node->markDirtyReferrerPoints();
}


template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  rt_assert_f(!rtHeap::useModifyFlag() || sizeof(T) == sizeof(oop) || 
      !_trackable_old_anchor || !rtHeap::is_modified(*p), 
      "modified field [%d] v = %x(%s)\n" PTR_DBG_SIG, 
      (int)((address)p - (address)_old_anchor_p), *(int32_t*)p, 
      RTGC::getClassName(CompressedOops::decode(*p)),
      PTR_DBG_INFO(_old_anchor_p));

  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  if (rtHeap::useModifyFlag() && _is_trackable_forwardee && sizeof(T) == sizeof(narrowOop)) {
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
  rt_assert_f(!old_p->isGarbageMarked(), "anchor = %p, mark=%p\n" PTR_DBG_SIG, old_p, cast_to_oop(old_p)->mark().to_pointer(), PTR_DBG_INFO(node));
  GCObject* new_obj = to_obj(cast_to_oop(old_p)->forwardee());
  if (new_obj != NULL) {
    rtgc_log(LOG_OPT(11), "anchor moved %p->%p in %p", old_p, new_obj, node);
    p[0] = new_obj;
  }  
}

size_t rtHeap::adjust_pointers(oopDesc* old_p) {
#ifdef ASSERT
  g_cntScan ++;
#endif

  GCObject* node = to_obj(old_p);
  // 모든 dead-space 는 명시적으로 garbage markin 되어 있다.
  bool is_alive = !node->isGarbageMarked();// : old_p->is_gc_marked();
  if (!is_alive) {
    // rt_assert(!old_p->is_gc_marked() || rtHeapUtil::is_dead_space(old_p));
    rtgc_log(true, "skip garbage %p", old_p);
    int size = old_p->size_given_klass(old_p->klass());
    return size;
  }

  oopDesc* new_anchor_p = NULL;
  bool is_trackable_forwardee = node->isTrackable_unsafe();
  g_adjust_pointer_closure._trackable_old_anchor = is_trackable_forwardee;
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
    rt_assert(old_p->forwardee() == NULL || !g_adjust_pointer_closure.is_in_young(old_p->forwardee()));
    // ensure UnsafeList is empty.
    rt_assert_f(!node->isUnreachable() || node->isUnstableMarked(), 
      "unreachable trackable %p(%s)", 
      old_p, getClassName(node));
  }

  rtgc_log(LOG_OPT(11), "adjust_pointers %p->%p", old_p, new_anchor_p);
  g_adjust_pointer_closure.init(old_p, new_anchor_p, is_trackable_forwardee);
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool is_young_root = g_adjust_pointer_closure._has_young_ref && is_trackable_forwardee;
  if (is_young_root) {
    oopDesc* forwardee = old_p->forwardee();
    if (forwardee == NULL) forwardee = old_p;
    rtgc_log(LOG_OPT(7), "add adjusted young root %p -> %p", old_p, forwardee);
    add_young_root(old_p, forwardee);
  }

  RtNode* nx = node->getMutableNode();
  if (nx->mayHaveAnchor()) {
    if (nx->hasMultiRef()) {
      ReferrerList* referrers = nx->getAnchorList();
      rt_assert_f(!referrers->isTooSmall() || nx->isAnchorListLocked(), 
          "invalid anchorList " PTR_DBG_SIG, PTR_DBG_INFO(node));
      for (ReverseIterator it(referrers); it.hasNext(); ) {
        ShortOOP* ptr = (ShortOOP*)it.next_ptr();
        adjust_anchor_pointer(ptr, node);
      }
    }
    else {
      adjust_anchor_pointer(&nx->getSingleAnchor(), node);
    }
  }

  node->unmarkDirtyReferrerPoints();
  return size; 
}


void rtHeap::prepare_adjust_pointers(HeapWord* old_gen_heap_start) {
  g_adjust_pointer_closure._old_gen_start = old_gen_heap_start;
  rtgc_log(LOG_OPT(2), "old_gen_heap_start %p", old_gen_heap_start);
  rt_assert(MarkSweep::_resurrect_stack.is_empty());
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

  if (!RTGC_FAT_OOP) {
    int cnt_root = g_stack_roots.size();
    if (cnt_root > 0) {
      GCObject** src = &g_stack_roots.at(0);
      GCObject** end = src + cnt_root;
      for (; src < end; src++) {
        GCObject* erased = src[0];
        oop new_p = cast_to_oop(erased)->forwardee();
        if (new_p != NULL) src[0] = to_obj(new_p);
      }
    }
  }
  GCNode::in_progress_adjust_pointers = true;
}

void rtHeap__onCompactionFinishied() {
  GCNode::in_progress_adjust_pointers = false;
}


void GCNode::markGarbage(const char* reason)  {
  if (reason != NULL) {
    rt_assert(this->isTrackable());
    rtgc_debug_log(this, "garbage marking on %p(%s) %s", this, getClassName(this), reason);
  }
  rt_assert_f(!this->isGarbageMarked(),
      "already marked garbage %p(%s)", this, getClassName(this));
  rt_assert_f(this->getRootRefCount() <= (reason != NULL ? ZERO_ROOT_REF : ZERO_ROOT_REF + 1),
      "invalid garbage marking on %p(%s) rc=%d", this, getClassName(this), this->getRootRefCount());
  rt_assert_f(!cast_to_oop(this)->is_gc_marked() || reason == NULL,
      "invalid garbage marking on %p(%s) rc=%d discovered=%p ghost=%d", this, getClassName(this), this->getRootRefCount(),
      __get_discovered(cast_to_oop(this)), rtHeapEx::print_ghost_anchors((GCObject*)this));

  flags().isGarbage = true;
  flags().isPublished = true;
}

bool rtHeapEx::print_ghost_anchors(GCObject* node, int depth) {
  if (depth > 0 && !rtHeap::is_alive(cast_to_oop(node)), false) return true;

  const int discovered_off = java_lang_ref_Reference::discovered_offset();
  AnchorIterator ai(node);
  rtgc_log(!ai.hasNext(), "no anchors for %p", node);

  while (ai.hasNext()) {
    const RtNode* nx = node->node_();
    if (nx->hasSafeAnchor()) {
      GCObject* anchor = nx->getSafeAnchor();
      bool isClass = cast_to_oop(anchor)->klass() == vmClasses::Class_klass();
      rtgc_log(1, "safe anchor[%d] %p(%s)[%d] unsafe:%d rc:%d gc_m:%d isClass=%d cldHolder=%p -> %p(%s) tr=%d", 
          depth, anchor, RTGC::getClassName(anchor),
          anchor->node_()->getShortcutId(), anchor->isUnstableMarked(), 
          anchor->getRootRefCount(), cast_to_oop(anchor)->is_gc_marked(), 
          isClass, !isClass ? NULL : (void*)cast_to_oop(anchor)->klass()->class_loader_data()->holder_no_keepalive(),
          node, RTGC::getClassName(node), node->isTrackable());
      print_ghost_anchors(anchor, depth + 1);
      return true;
    }
    GCObject* anchor = ai.next();
    if (!anchor->isGarbageMarked()) {//} && !is_java_reference(cast_to_oop(anchor), (ReferenceType)-1)) {
      bool isClass = cast_to_oop(anchor)->klass() == vmClasses::Class_klass();
      rtgc_log(1, "ghost anchor[%d] %p(%s)[%d] unsafe:%d rc:%d gc_m:%d isClass=%d cldHolder=%p -> %p(%s) tr=%d", 
          depth, anchor, RTGC::getClassName(anchor),
          anchor->node_()->getShortcutId(), anchor->isUnstableMarked(), 
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


void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
#ifdef ASSERT  
  if (is_alive(p, false) || (node->isTrackable() ? !node->isUnreachable() : node->node_()->hasAnchor())) {
    rtHeapEx::print_ghost_anchors(to_obj(p));
  }
#endif

  rt_assert_f(is_destroyed(p), "wrong on garbage %p[%d](%s) unreachable=%d tr=%d rc=%d ac=%d isUnsafe=%d ghost=%d", 
        node, node->node_()->getShortcutId(), RTGC::getClassName(node), node->isUnreachable(),
        node->isTrackable(), node->getRootRefCount(), node->getReferrerCount(), node->isUnstableMarked(),
        rtHeapEx::print_ghost_anchors(to_obj(p)));
  rt_assert(node->isTrackable() ? node->isUnreachable() : node->getReferrerCount() == 0);
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
  if (RTGC_FAT_OOP) {
    rtHeap__clearStack<true>();
  }
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

void rtHeap::prepare_rtgc() {
  rtgc_log(LOG_OPT(1), "prepare_rtgc\n");
  debug_only(cnt_resurrect = 0;);
  is_gc_started = true;
  rt_assert(g_stack_roots.size() == 0);
  if (rtHeap::useModifyFlag()) {
    UpdateLogBuffer::process_update_logs();
  }
  g_saved_young_root_count = g_young_roots.size();
#if TRACE_UPDATE_LOG
  log_trace_p(gc)("field_update count = %d, inverse_graph_update_cnt = %d", g_field_update_cnt, g_inverse_graph_update_cnt);
  g_field_update_cnt = 0;
  g_inverse_graph_update_cnt = 0;
#endif
}

void rtHeap::init_reference_processor(ReferencePolicy* policy) {
  rt_assert(is_gc_started);
  rtgc_log(LOG_OPT(1), "init_reference_processor %p", policy);
  if (policy != NULL) {
    FreeMemStore::clearStore();
    if (RtLazyClearWeakHandle) {
      WeakProcessor::oops_do(&clear_weak_handle_ref);
    }
    in_full_gc = 1;
    g_in_progress_marking = true;
    rtHeapEx::break_reference_links(policy);
  }
  rtHeap__processUntrackedTenuredObjects();
}


void rtHeap::finish_rtgc(bool is_full_gc_unused, bool promotion_finished_unused) {
  rt_assert(GCNode::g_trackable_heap_start == GenCollectedHeap::heap()->old_gen()->reserved().start());
  rtgc_log(LOG_OPT(1), "finish_rtgc full_gc=%d", in_full_gc);
  is_gc_started = false;
  if (!RTGC_FAT_OOP || !in_full_gc) {
    // link_pending_reference 수행 시, mark_survivor_reachable() 이 호출될 수 있다.
    rtHeap__clearStack<false>();
  }
  if (rtHeap::useModifyFlag()) {
    UpdateLogBuffer::reset_gc_context();
  }
  rtHeapEx::g_lock_unsafe_list = false;
  rt_assert(g_stack_roots.size() == 0);
  RuntimeHeap::reclaimSpace();
  rt_assert(MarkSweep::_resurrect_stack.size() == 0);
  MarkSweep::_resurrect_stack.clear();
  in_full_gc = 0;
}


// void rtHeap::lock_jni_handle(oopDesc* p) {
//   if (!REF_LINK_ENABLED) return;
//   rtgc_debug_log(p, "lock_handle %p", p);
//   GCRuntime::onAssignRootVariable(to_obj(p));
// }

// void rtHeap::release_jni_handle(oopDesc* p) {
//   if (!REF_LINK_ENABLED) return;
//   rt_assert_f(to_obj(p)->isStrongRootReachable(), "wrong handle %p(%s) isClass=%d", p, RTGC::getClassName(to_obj(p)), p->klass() == vmClasses::Class_klass());
//   rtgc_debug_log(p, "release_handle %p", p);
//   GCRuntime::onEraseRootVariable_internal(to_obj(p));
// }

void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d", 
      g_cntTrackable, g_young_roots.size(), full_gc); 
}

// copyed from CollectedHeap.cpp
size_t CollectedHeap::filler_array_hdr_size() {
  return align_object_offset(arrayOopDesc::header_size(T_INT)); // align to Long
}

size_t CollectedHeap::filler_array_min_size() {
  return align_object_size(filler_array_hdr_size()); // align to MinObjAlignment
}

void rtHeap__mark_dead_space(oopDesc* deadObj) {
  GCObject* obj = to_obj(deadObj);
  rt_assert(!deadObj->is_gc_marked());
  if (obj->isTrackable_unsafe()) {
    rt_assert(obj->isDestroyed());
  } else {
    rt_assert(obj->isUnreachable());
    obj->markGarbage(NULL);
    obj->markDestroyed();
  }
}

void rtHeap__ensure_trackable_link(oopDesc* anchor, oopDesc* obj) {
  if (anchor != obj) {
    rt_assert_f(rtHeap::is_alive(obj), "must not a garbage \n" PTR_DBG_SIG, PTR_DBG_INFO(obj)); 
    rt_assert_f(to_obj(obj)->hasReferrer(to_obj(anchor)), 
        "anchor=" PTR_DBG_SIG "link=" PTR_DBG_SIG,
        PTR_DBG_INFO(anchor), PTR_DBG_INFO(obj)); 
  }
}

class ImmortalMarkClosure : public ObjectClosure {
  public: int _cnt; 
  ImmortalMarkClosure() { _cnt = 0; }
  void do_object(oop obj) { _cnt++; to_obj(obj)->markImmortal(); }
};

class CheckImmortalClosure : public ObjectClosure {
  public: int _cnt;
  CheckImmortalClosure() { _cnt = 0; }
  void do_object(oop obj) { _cnt++; rt_assert_f(!to_obj(obj)->isImmortal(), "not immortal %p", (void*)obj); }
};

static const bool ENABLE_CDS = false;
void rtHeapEx::mark_immortal_heap_objects() {
  if (!ENABLE_CDS) return;
  ImmortalMarkClosure immortalMarkClosure ;
  SerialHeap* heap = SerialHeap::heap();
  heap->old_gen()->object_iterate(&immortalMarkClosure);
  rtgc_log(true, "immortalMarkClosure %d", immortalMarkClosure._cnt);
}

void rtHeapEx::check_immortal_heap_objects() {
  if (!ENABLE_CDS) return;
  // rm build/macosx-x86_64-client-fastdebug/images/jdk/lib/client/classes.jsa
  CheckImmortalClosure checkImmortalClosure ;
  SerialHeap* heap = SerialHeap::heap();
  heap->old_gen()->object_iterate(&checkImmortalClosure);
  rtgc_log(true, "checkImmortalClosure %d", checkImmortalClosure._cnt);
}

RtHashLock::RtHashLock() {
  _hash = 0;
}

bool RtHashLock::isCodeFixed(int32_t hash) {
  return (hash & ANCHOR_LIST_UNLOCKED) == 0;
}

intptr_t RtHashLock::initHash(markWord mark) {
  RtNode* nx = (RtNode*)&mark;
  if (nx->hasMultiRef()) {
    releaseHash();
    int hash = nx->getIdentityHashCode();
    if (!isCodeFixed(hash)) {
      _hash = hash;
      return 0;
    } 

    hash &= ~ANCHOR_LIST_UNLOCKED;
    return hash;
  }

  if (_hash == 0) {
    _hash = allocateHashSlot(nx->mayHaveAnchor() ? &nx->getSingleAnchor() : NULL);
    rt_assert(isCodeFixed(_hash));
  }
  return 0;
}

int RtHashLock::allocateHashSlot(ShortOOP* first) {
  RTGC::lock_heap();
  ReferrerList* refList = ReferrerList::allocate();
  int hash = ReferrerList::getIndex(refList);
  RTGC::unlock_heap();
  if (first == NULL) {
    refList->initEmpty();
    rt_assert(refList->empty());
    rt_assert(refList->approximated_item_count() == 0);
  } else {
    refList->init(*first);
    rt_assert(refList->approximated_item_count() == 1);
  }
  return hash & ~ANCHOR_LIST_UNLOCKED;
}

intptr_t RtHashLock::hash() {
  rt_assert(_hash != 0);
  return (intptr_t)(_hash & ~ANCHOR_LIST_UNLOCKED);
}

void RtHashLock::consumeHash(intptr_t hash) { 
  if (this->hash() == hash) _hash = 0; 
}

void RtHashLock::releaseHash() {
  if (_hash == 0) return;
  if (isCodeFixed(_hash)) {
    RTGC::lock_heap();
    ReferrerList::delete_(ReferrerList::getPointer(_hash));
    RTGC::unlock_heap();
  }
  _hash = 0;
}

RtHashLock::~RtHashLock() {
  releaseHash();
}


void rtHeap::oop_recycled_iterate(ObjectClosure* closure) {
  // fatal("full gc 도 oop_recycled_iterate() 호출해야 한다.");
  // rt_assert(!in_full_gc || g_recycled.size() == 0);
  for (int idx = 0; idx < g_recycled.size(); idx++) {
    GCObject* node = g_recycled.at(idx);
    rt_assert(node->isTrackable());
    rt_assert(!node->isYoungRoot());
    if (node->isGarbageMarked()) {
      debug_only(rt_assert(in_full_gc && idx < g_debug_cnt_untracked));
    } else {
      rtgc_log(LOG_OPT(7), "oop_recycled_iterate %p", node);
      closure->do_object(cast_to_oop(node));
    }
  }
#ifdef ASSERT  
  g_debug_cnt_untracked = 0;
#endif
  g_recycled.resize(0); 
}

int cnt_init = 0;
void rtHeap__initialize() {
  rtgc_log(true, "trackable_heap_start = %p narrowOpp:base = %p", 
    GCNode::g_trackable_heap_start, CompressedOops::base());

  g_young_roots.initialize();
  g_stack_roots.initialize();
  g_recycled.initialize();
  rtCLDCleaner::initialize();
  UpdateLogBuffer::reset_gc_context();

}

