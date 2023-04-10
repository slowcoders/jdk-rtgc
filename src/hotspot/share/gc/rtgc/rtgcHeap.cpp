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
#include "gc/rtgc/impl/GCUtils2.hpp"


int rtHeap::in_full_gc = 0;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

using namespace RTGC;

namespace RTGC {
  const bool BATCH_UPDATE_ANCHORS = false & MARK_ALIVE_CHUNK;
  // bool yg_root_locked = false;
  bool g_in_progress_marking_unstable_young_root = false;

  int cnt_resurrect = 0;
  int g_cnt_skip_garbage = 0;
  int g_cnt_multi_anchored_obj;
  int g_cnt_ref_anchor;
  int g_cnt_alive_obj;

  extern bool REF_LINK_ENABLED;
  bool ENABLE_GC = true && REF_LINK_ENABLED;
  bool is_gc_started = false;
  Stack<GCObject*, mtGC> _yg_root_reachables;

  class RtAdjustPointerClosure: public BasicOopIterateClosure {
  public:
    template <typename T> void do_oop_work(T* p);
    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
    virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }

    bool is_in_young(void* p) { return p < _old_gen_start; }
    void init(oopDesc* old_node_p, oopDesc* new_node_p) { 
      _old_anchor_p = old_node_p; 
      _new_anchor_p = new_node_p; 
      _has_young_ref = false; 
    }
    bool _has_young_ref;
    bool _was_young_root;
    bool _is_promoted;
    HeapWord* _old_gen_start;
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
  int g_saved_stack_root_count = 0;
  int g_marked_root_count = 0;
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

void rtHeap::init_allocated_object(HeapWord* mem, Klass* klass) {
#if RTGC_FAT_OOP
  mem[1] = 0;
#endif
  oopDesc::set_klass_gap(mem, 0);
}      

void rtHeap::init_mark(oopDesc* p) {
  if (UseBiasedLocking) {  
    p->set_mark(markWord::prototype_for_klass(p->klass()));
  } else {
    p->set_mark(markWord::prototype());
  }
}

bool rtHeap::is_alive(oopDesc* p, bool must_not_destroyed) {
  rt_assert(EnableRTGC);
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
  rtgc_debug_log(old_p, "add YG Root (%p)->%p idx=%d", old_p, new_p, g_young_roots.size());
  g_young_roots.push_back(new_p);
}

bool rtHeap::is_trackable(oopDesc* p) {
  if (!EnableRTGC) return false;
  GCObject* obj = to_obj(p);
  bool isTrackable = obj->isTrackable_unsafe();
  return isTrackable;
}

bool rtHeap::is_in_trackable_space(void* p) {
  return GenCollectedHeap::heap()->old_gen()->is_in(p);
}

void rtHeap::lock_jni_handle_at_safepoint(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onAssignRootVariable_internal(obj);
}

void rtHeap::release_jni_handle_at_safepoint(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onEraseRootVariable_internal(obj);
}


void rtHeap::mark_promoted_trackable(oopDesc* new_p) {
  // GC 수행 도중에 old-g로 옮겨진 객체들을 marking 한다.
  GCObject* node = to_obj(new_p);
  if (!AUTO_TRACKABLE_MARK_BY_ADDRESS) {
    rt_assert(!node->isTrackable());
    node->markTrackable();
  } else {
    rt_assert(node->is_adjusted_trackable());
  }
  // rtgc_debug_log(new_p, "mark_promoted_trackable %p->%p", new_p, (void*)new_p->forwardee());
  rtCLDCleaner::lock_cld(new_p);
}


void rtHeap::mark_young_root(oopDesc* tenured_p, bool is_young_root) {
  GCObject* node = to_obj(tenured_p);
  // full-gc marking 단계에서는 Weak/Soft Reference 를 scan 하지 않는다.
  // 이에, youngRoot 추가만 하고, 삭제는 하지 않는다.
  if (is_young_root && !node->isYoungRoot()) {
    rtHeap::add_young_root(tenured_p, tenured_p);
  }
}

void rtHeapUtil::resurrect_young_root(GCObject* node) {
  rtgc_log((++cnt_resurrect % 100) == 0 || RTGC::is_debug_pointer(node), "resurrect_young_root cnt=%d %p", cnt_resurrect, node);
  rt_assert(node->isGarbageMarked());
  rt_assert(node->isTrackable());
  if (!rtHeap::in_full_gc) {
    rt_assert(node->isYoungRoot() && node->isDirtyReferrerPoints());
  }
  node->unmarkGarbage();
  node->unmarkDirtyReferrerPoints();  
  node->invalidateSafeAnchor();
  MarkSweep::_resurrect_stack.push(cast_to_oop(node));
  if (rtHeap::in_full_gc) {
    rtCLDCleaner::resurrect_cld(cast_to_oop(node));
  }
}

void rtHeap__addRootStack_unsafe(GCObject* node) {
  rtgc_log(LOG_OPT(9), "add stack root %p rc=%x", node, node->getRootRefCount());
  rt_assert(node->is_adjusted_trackable());
  node->markSurvivorReachable_unsafe();
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

void rtHeap::mark_young_root_reachable(oopDesc* anchor_p, oopDesc* link_p) {
  // rtgc_log(true, "mark_young_root_reachable %p -> %p = %d", anchor_p, link_p, g_in_progress_marking_unstable_young_root);
  if (!g_in_progress_marking_unstable_young_root) return;
  if (anchor_p == link_p) return;
  GCObject* anchor = to_obj(anchor_p);
  GCObject* link = to_obj(link_p);
  rt_assert_f(anchor->isTrackable(), "not tenured anchor %p(%s)", anchor_p, anchor_p->klass()->name()->bytes());
  rt_assert_f(!link->isTrackable(), "not young link %p(%s)", anchor_p, anchor_p->klass()->name()->bytes());
  rt_assert_f(!anchor->isGarbageMarked(), "grabage anchor %p(%s)", anchor_p, anchor_p->klass()->name()->bytes());
  if (link->isAcyclic()) return;
  link->addTemporalAnchor(anchor);
}

void rtHeap__ensure_resurrect_mode() {
  rt_assert(g_in_progress_marking_unstable_young_root);
}

void rtHeap::mark_young_survivor_reachable(oopDesc* anchor_p, oopDesc* link_p) {
  // rtgc_log(true, "mark_young_survivor_reachable %p -> %p", anchor_p, link_p);
  rt_assert(g_in_progress_marking_unstable_young_root);
  rt_assert(g_marked_root_count == 0);
  if (anchor_p == link_p) return;
  GCObject* anchor = to_obj(anchor_p);
  rt_assert_f(!anchor->isTrackable(), "not young anchor %p(%s)", anchor_p, anchor_p->klass()->name()->bytes());
  rt_assert_f(!anchor->isGarbageMarked(), "grabage anchor %p(%s)", anchor_p, anchor_p->klass()->name()->bytes());

  GCObject* link = to_obj(link_p);
  if (!link->isTrackable()) {
    link->addTemporalAnchor(anchor);
  } else {
    if (link->getRootRefCount() > 0) {
      if (link->isSurvivorReachable()) return;

      if (link->isAcyclic()) {
        // acyclic tenured link 가 이미 refCount 를 가지고 있는 경우, findSurvivorPath 수행이 불가하다.
        // refCount 를 swap 하여 0 으로 변경하면, old 영역의 path 검색이 불가능하기 때문이다..
        // RTGC-TODO. 임시로 acyclic marking 한 객체를 따로 모은 후, GC 종료 단계에서 예외 처리하다.
        rtHeap__addRootStack_unsafe(link);
        return;
      }
    }

    if (link->addDirtyAnchor(anchor)) {
      rtHeap__addRootStack_unsafe(link);
      //g_stack_roots.push_back(link);
    }
  }
}

void rtHeap::mark_survivor_reachable(oopDesc* new_p) {
  rt_assert(in_full_gc || !g_in_progress_marking_unstable_young_root);
  rt_assert(EnableRTGC);
  // rt_assert(in_full_gc || g_marked_root_count == 0);
  GCObject* node = to_obj(new_p);
  rt_assert_f(node->is_adjusted_trackable(), "must be trackable" PTR_DBG_SIG, PTR_DBG_INFO(new_p));
  rt_assert(!node->isGarbageMarked());
  if (false && node->isGarbageMarked()) {
    rt_assert_f(node->isTrackable(), "not yr " PTR_DBG_SIG, PTR_DBG_INFO(node));
    rtHeapUtil::resurrect_young_root(node);
    if (node->hasSafeAnchor()) return;
    // garbage marking 된 상태는 stack marking 이 끝난 상태.
  }

  if (!node->isSurvivorReachable()) {
    rtHeap__addRootStack_unsafe(node);
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
      // rtgc_log(true, "clear stack root %p", erased);
      rt_assert(erased->is_adjusted_trackable());
      rt_assert_f(erased->isSurvivorReachable(), "cls %p " PTR_DBG_SIG, 
          (void*)cast_to_oop(erased)->klass()->java_mirror_no_keepalive(), PTR_DBG_INFO(erased));
      if (erased->unmarkSurvivorReachable() <= ZERO_ROOT_REF) {
        if (!erased->hasSafeAnchor() && !erased->isUnstableMarked()) {
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


void rtHeap::clear_temporal_anchor_list(oopDesc* old_p) {
  GCObject* old = to_obj(old_p);
  rt_assert(!old->isTrackable());
  ((HeapWord*)old)[1] = 0;
}

void rtHeap__clear_garbage_young_roots(bool is_full_gc) {
  if (true) {
    // dirty links 에 대한 GC 를 먼저 실행한 후, dirtyAnchor 들을 모두 제거한다. (더 이상 survivalPath 검색에 불필요.)
    GCObject** dirtyLinks = &g_stack_roots.at(g_saved_stack_root_count);
    int cnt_dirty_link = g_stack_roots.size() - g_saved_stack_root_count;
    _rtgc.g_pGarbageProcessor->scanGarbage(dirtyLinks, cnt_dirty_link);

    for (; --cnt_dirty_link >= 0; ) {
      GCObject* node = *dirtyLinks++;
      if (!node->isGarbageMarked()) {
        node->removeDirtyAnchors();
        if (node->isSurvivorReachable()) {
          g_stack_roots.at(g_saved_stack_root_count++) = node;
        }
      }
    }
    g_stack_roots.resize(g_saved_stack_root_count);
    /* Now no object has dirty anchors. */
  }
  //
  ReferrerList::clearTemporalChunkPool();


  if (!is_full_gc) {
    // _rtgc.g_pGarbageProcessor->validateGarbageList();
  }
  _rtgc.g_pGarbageProcessor->collectAndDestroyGarbage(is_full_gc);
  
  if (RTGC_DEBUG) {
    for (int idx_root = g_marked_root_count; idx_root < g_saved_young_root_count; idx_root ++) {
      GCObject* node = to_obj(g_young_roots.at(idx_root));
      if (!node->isGarbageMarked()) {
        AnchorState state = _rtgc.g_pGarbageProcessor->checkAnchorStateFast(node, false);
        rt_assert_f(state == AnchorState::NotAnchored, "misdetected garbage root %p/%d " PTR_DBG_SIG, node, idx_root, PTR_DBG_INFO(node));
        rt_assert_f(node->isGarbageMarked(), "misdetected garbage root %p/%d " PTR_DBG_SIG, node, idx_root, PTR_DBG_INFO(node));
      }
  }
  }

  // while (!_yg_root_reachables.is_empty()) {
  //   GCObject* obj = _yg_root_reachables.pop();
  //   if (obj->hasMultiRef()) {
  //     ReferrerList* list = obj->getAnchorList();
  //     if (false) {
  //       rtgc_log(true, "destroy ref_list %p %p:%p/%d (%s)", obj, list, list->lastItemPtr(), list->approximated_item_count(), getClassName(obj));
  //       if(list->hasMultiChunk()) {
  //         ReferrerList::Chunk* chunk = ReferrerList::getContainingChunck(list->lastItemPtr());
  //         while (chunk != &list->_head) {
  //           rtgc_log(true, "  chunk %p", chunk);
  //           chunk = chunk->getNextChunk();
  //         }
  //       }
  //     }
  //     ReferrerList::delete_(list);
  //     obj->setHasMultiRef(false);
  //   }
  //   obj->removeSingleAnchor();
  // }

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
    
    // young-gc 실패 후, 연이어 full_gc 가 수행되는 것을 대비.
    g_saved_young_root_count = g_young_roots.size();
    rtHeap__clearStack<false>();

    rtHeapEx::g_lock_garbage_list = true;
  } else {
    // rtCLDCleaner::clear_cld_locks(g_young_root_closure);
    // rtCLDCleaner::collect_garbage_clds(g_young_root_closure);
    // _rtgc.g_pGarbageProcessor->collectAndDestroyGarbage(is_full_gc);
  }
}

template <bool is_root_reachable> 
inline int mark_young_root_reachables(int marked_root_count, RtYoungRootClosure* closure, bool is_full_gc) {
  int cnt_stack_root = -1;
  int young_root_count = g_saved_young_root_count;
  rt_assert(young_root_count <= g_young_roots.size());
  debug_only(g_in_progress_marking_unstable_young_root = !is_root_reachable;)

  for (bool need_rescan = true; cnt_stack_root < g_stack_roots.size(); ) {
    cnt_stack_root = g_stack_roots.size();
    rtgc_log(true, "mark young roots %s n %d / y0 %d / y2 %d", 
        is_root_reachable ? "strong" : "uncertain",
        marked_root_count, young_root_count, g_young_roots.size());
    bool use_shortcut = marked_root_count == 0;

    for (int idx_root = marked_root_count; idx_root < young_root_count; idx_root ++) {
      GCObject* node = to_obj(g_young_roots.at(idx_root));
      AnchorState state = _rtgc.g_pGarbageProcessor->checkAnchorStateFast(node, use_shortcut);
      if (is_root_reachable ? (state == AnchorState::AnchoredToRoot) : (state != AnchorState::NotAnchored)) {
        rtgc_debug_log(node, "scan young root %p %d", node, idx_root);
        bool is_young_root = closure->iterate_tenured_young_root_oop(cast_to_oop(node), is_root_reachable);
        closure->do_complete(is_root_reachable);
        if (!is_root_reachable) {
          cnt_stack_root = -1;
        }
        if (!is_full_gc && !is_young_root) {
          node->unmarkYoungRoot();
        }

        if (marked_root_count != idx_root) {     
          g_young_roots.swap(marked_root_count, idx_root);
        }
        marked_root_count ++;
      }
    }
  };
  debug_only(g_in_progress_marking_unstable_young_root = false;)
  return marked_root_count;
}

void rtHeap::iterate_younger_gen_roots(RtYoungRootClosure* closure, bool is_full_gc) {
  g_young_root_closure = closure;
  int young_root_count = g_saved_young_root_count; //is_full_gc ? g_young_roots.size() : g_saved_young_root_count;
  rtHeapEx::g_lock_garbage_list = false;

#ifdef ASSERT
  for (int idx_root = 0; idx_root < g_young_roots.size(); idx_root++ ) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
    rtgc_log(LOG_OPT(99), "registered young root %p %d", node, idx_root);

    rt_assert_f(!node->isGarbageMarked(), "invalid yg-root %p(%s)", node, RTGC::getClassName(node));
  }
#endif

  int marked_root_count = mark_young_root_reachables<true>(0, closure, is_full_gc);
  // !!! save valid stack root count 
  g_saved_stack_root_count = g_stack_roots.size();

  marked_root_count = mark_young_root_reachables<false>(marked_root_count, closure, is_full_gc);
  if (rtHeapEx::keep_alive_young_final_referents(closure, is_full_gc)) {
    // YG-referent 만 한정하며, weak/soft ref 의 가비지 여부 검사와 관계없이 keep_alive 처리가 가능하다.
    marked_root_count = mark_young_root_reachables<true>(marked_root_count, closure, is_full_gc);
    marked_root_count = mark_young_root_reachables<false>(marked_root_count, closure, is_full_gc);
  }

  g_marked_root_count = marked_root_count;

  if (RTGC_DEBUG) {
    for (int idx_root = g_marked_root_count; idx_root < g_saved_young_root_count; idx_root ++) {
    GCObject* node = to_obj(g_young_roots.at(idx_root));
      rtgc_debug_log(node, "skipped young root %p %d", node, idx_root);
      AnchorState state = _rtgc.g_pGarbageProcessor->checkAnchorStateFast(node, false);
      rt_assert(state == AnchorState::NotAnchored);
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
  rt_assert(node->isAcyclic() || node->hasReferrer(to_obj(anchor)));

  if (node->isGarbageMarked()) {
    rt_assert_f(node->hasAnchor() || (node->isYoungRoot() && node->isDirtyReferrerPoints()), 
        "invalid link %p(%s) -> %p(%s)", 
        anchor, RTGC::getClassName(to_obj(anchor)), node, RTGC::getClassName(node));
    rtHeapUtil::resurrect_young_root(node);
    node->setSafeAnchor(to_obj(anchor));
  }
}


void rtHeap::add_trackable_link(oopDesc* anchor_p, oopDesc* link_p) {
  // rtgc_log(true, "add trackble link %p - %p", anchor_p, link_p);
  GCObject* link = to_obj(link_p);
  GCObject* anchor = to_obj(anchor_p);
  if (anchor == link) return;

  rt_assert(link->isTrackable());
  rt_assert(anchor->isTrackable());
  rt_assert_f(!anchor->isGarbageMarked(), "grabage anchor %p(%s)", 
      anchor, anchor_p->klass()->name()->bytes());
  rt_assert_f(!link->isGarbageMarked(), "grabage link " PTR_DBG_SIG "\n anchor" PTR_DBG_SIG, PTR_DBG_INFO(link), PTR_DBG_INFO(anchor));
  if (false && link->isGarbageMarked()) {
    rt_assert_f(link->hasAnchor() || (link->isYoungRoot() && link->isDirtyReferrerPoints()), 
        "invalid link_p %p(%s) -> %p(%s)", 
        anchor_p, RTGC::getClassName(anchor), link, RTGC::getClassName(link));

    rt_assert_f(in_full_gc || link->isYoungRoot(), "no y-root %p(%s)",
        link, RTGC::getClassName(link));
    rtHeapUtil::resurrect_young_root(link);
  }

  // rtgc_debug_log(link_p, "add_trackable_link anchor_p %p link_p: %p", anchor_p, link_p);
  link->addTrackableAnchor(anchor);
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

    // TODO markDirty 시점이 너무 이름. 필요없다??
    // rtgc_debug_log(p, "mark_forwarded %p", p);
    // 주의) mayHaveAnchor 가 아닌 hasAnchor 를 정확히 사용.
    if (node->hasAnchor()) {
      node->markDirtyReferrerPoints();
    }
  } else {
    rt_assert(p->is_gc_marked());
    rtHeap::clear_temporal_anchor_list(p);
  }
}


template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  rt_assert_f(!rtHeap::useModifyFlag() || 
      !to_obj(_old_anchor_p)->isTrackable_unsafe() || !rtHeap::is_modified(*p), 
      "modified field [%d] v = %p(%s)" PTR_DBG_SIG, 
      (int)((address)p - (address)_old_anchor_p), (void*)CompressedOops::decode(*p), 
      RTGC::getClassName(CompressedOops::decode(*p)),
      PTR_DBG_INFO(_old_anchor_p));

  // rtgc_debug_log(_old_anchor_p, "adjust_pointer (%p/%s)[%d]", 
  //     _old_anchor_p, _old_anchor_p->klass()->name()->bytes(),
  //     (int)((address)p - (address)_old_anchor_p));
  if (RTGC_DEBUG) {
    T heap_oop = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(heap_oop)) {
      oop obj = CompressedOops::decode_not_null(heap_oop);
      assert(Universe::heap()->is_in(obj), "should be in heap");
      oop new_obj = cast_to_oop(obj->mark().decode_pointer());
      rt_assert_f(new_obj == NULL || new_obj > (void*)10, "adjust_pointer [%d] " PTR_DBG_SIG, 
          (int)((address)p - (address)_old_anchor_p), PTR_DBG_INFO(_old_anchor_p));
      narrowOop x = CompressedOops::encode(new_obj);
    }
  }

  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  rt_assert_f(old_p == NULL || rtHeap::is_alive(old_p, false) || 
      (!rtHeap::is_trackable(old_p) && (void*)new_p == old_p), // 위치 이동되지 않은 young 객체일 수 있다.
      PTR_DBG_SIG " -> %p\n anchor was_yg_root %d " PTR_DBG_SIG, PTR_DBG_INFO(old_p), (void*)new_p, _was_young_root,  PTR_DBG_INFO(_old_anchor_p));
  if (rtHeap::useModifyFlag() && _new_anchor_p != NULL) {
    *p = rtHeap::to_unmodified(*p);
  }

  // rtgc_debug_log(_old_anchor_p, "adjust_pointer (%p/%p wasRoot=%d)[%d] -> %p/%p(_s))", 
  //     _old_anchor_p, (void*)_new_anchor_p, _was_young_root, 
  //     (int)((address)p - (address)_old_anchor_p), old_p, (void*)new_p);//, old_p->klass()->name()->bytes());

  if (old_p == NULL || old_p == _old_anchor_p) return;

  rt_assert_f(_was_young_root || !to_obj(_old_anchor_p)->isTrackable_unsafe() || to_obj(old_p)->isTrackable_unsafe(),
     "adjust_pointer %p(%s ygRoot=%d tr=%d)[%d] -> %p/%p(%s))", 
      _old_anchor_p, _old_anchor_p->klass()->name()->bytes(), _was_young_root, _new_anchor_p != NULL, 
      (int)((address)p - (address)_old_anchor_p), old_p, (void*)new_p, old_p->klass()->name()->bytes());

#ifdef ASSERT
  rtHeapUtil::ensure_alive_or_deadsapce(old_p, _old_anchor_p);
#endif   

  if (_new_anchor_p == NULL) return;
  if (!to_obj(new_p)->isTrackable_unsafe()) {
    this->_has_young_ref = true;
    return;
  }
  if (!_is_promoted && to_obj(old_p)->isTrackable_unsafe()) return;

  
  int max_count = (1 << 21) - 1;
  if ((to_obj(old_p)->getRootRefCount() & max_count) >= max_count/3) {
      rtgc_log((to_obj(old_p)->getRootRefCount() % 1000) == 0, "big array %p %d" PTR_DBG_SIG, 
          (void*)old_p, to_obj(old_p)->getRootRefCount(), PTR_DBG_INFO(_old_anchor_p));
      // if (_old_anchor_p->klass()->is_objArray_klass()) {
      //     objArrayOopDesc* array = (objArrayOopDesc*)_old_anchor_p;
      //     if (array->length() > 10*1000) {
      //         rtgc_log(true, "array-length %d", array->length());
      //         int cnt_dup = 0;
      //         for (int i = 0; i < array->length(); i ++) {
      //             if (array->obj_at(i) == (void*)this) cnt_dup ++;
      //         }
      //         rtgc_log(true, "item in array %d", cnt_dup);
      //         // rt_assert_f(false, "debug-stop " PTR_DBG_SIG, PTR_DBG_INFO(this));
      //     }
      // }
  }

  // old_p 내부 field 에 대한 adjust_pointers 가 처리되지 않았으면...
  if (BATCH_UPDATE_ANCHORS || to_obj(old_p)->isDirtyReferrerPoints()) {
    // old_p 에 대해 adjust_pointers 를 수행하기 전.
    to_obj(old_p)->addTrackableAnchor(to_obj(_old_anchor_p));
  }
  else {
    to_obj(old_p)->addTrackableAnchor(to_obj(_new_anchor_p));
    debug_only(g_cnt_ref_anchor += (int)to_obj(old_p)->mayHaveAnchor();)
  }
}


static void adjust_anchor_pointer(ShortOOP* p, GCObject* node) {
  GCObject* old_p = p[0];
  rt_assert_f(!old_p->isGarbageMarked(), "anchor = %p, mark=%p" PTR_DBG_SIG, 
      old_p, cast_to_oop(old_p)->mark().to_pointer(), PTR_DBG_INFO(old_p));
  debug_only(g_cnt_ref_anchor++;)
  GCObject* new_obj = to_obj(cast_to_oop(old_p)->forwardee());
  if (new_obj != NULL) {
    rtgc_log(LOG_OPT(11), "anchor moved %p->%p in %p", old_p, new_obj, node);
    p[0] = new_obj;
  }  
}


size_t rtHeap::adjust_pointers(oopDesc* old_node_p) {

  GCObject* old_node = to_obj(old_node_p);
  // 참고) 모든 dead-space 는 trakable 이 아니어도 명시적으로 garbage marking 되어야 한다.
  if (!old_node->isAlive()) {
    // rt_assert(!old_node_p->is_gc_marked() || rtHeapUtil::is_dead_space(old_node_p));
    rtgc_log((++g_cnt_skip_garbage % 1000) == 0 || RTGC::is_debug_pointer(old_node), "skip garbage %p cnt_skip=%d", old_node_p, g_cnt_skip_garbage);
    int size = old_node_p->size_given_klass(old_node_p->klass());
    return size;
  }
  // 참고) 주소가 옮겨지지 않은 YG 객체는 unmarked 상태이다.

  rtgc_debug_log(old_node, "adjust_pointers %p(ac=%d)", old_node, old_node->getAnchorCount());
#if 0 // def ASSERT  
  if (RTGC::is_debug_pointer(old_node) && old_node->hasMultiRef()) {
    for (AnchorIterator ai(old_node); ai.hasNext(); ) {
      rtgc_log(1, "  anchor %p", (GCObject*)ai.next());
    }
  }
#endif
  debug_only(g_cnt_alive_obj++;);
  oopDesc* new_node_p = old_node_p->forwardee();
  if (new_node_p == NULL) new_node_p = old_node_p;
  g_adjust_pointer_closure._is_promoted = false;
  if (!old_node->isTrackable_unsafe()) {
    rt_assert(!to_obj(old_node_p)->isYoungRoot());
    g_adjust_pointer_closure._was_young_root = false;
    if (g_adjust_pointer_closure.is_in_young(new_node_p)) {
      new_node_p = NULL;
    } else {
      g_adjust_pointer_closure._is_promoted = true;
      mark_promoted_trackable(old_node_p);
      if (old_node->isUnreachable()) {
        // GC 종료 후 UnsafeList 에 추가.
        mark_survivor_reachable(old_node_p);
      }
    }
  } else {
    rt_assert_f(!g_adjust_pointer_closure.is_in_young(new_node_p), "tenured object 는 YG 로 옮겨질 수 없다.");
    g_adjust_pointer_closure._was_young_root = to_obj(old_node_p)->isYoungRoot();
    if (g_adjust_pointer_closure._was_young_root) {
      to_obj(old_node_p)->unmarkYoungRoot();
    }
    // ensure UnsafeList is empty.
    rt_assert_f(!old_node->isUnreachable() || old_node->isUnstableMarked(), 
      "unreachable trackable %p(%s)", 
      old_node_p, getClassName(old_node));
  }

  rtgc_log(LOG_OPT(11), "adjust_pointers %p->%p", old_node_p, new_node_p);
  g_adjust_pointer_closure.init(old_node_p, new_node_p);
  size_t size = old_node_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool is_young_root = g_adjust_pointer_closure._has_young_ref;
  if (is_young_root) {
    rt_assert(new_node_p != NULL);
    add_young_root(old_node_p, new_node_p);
  }

  if (old_node->isDirtyReferrerPoints()) {
    if (old_node->hasMultiRef()) {
      debug_only(g_cnt_multi_anchored_obj ++;)
      if (!BATCH_UPDATE_ANCHORS) {
        ReferrerList* referrers = old_node->getAnchorList();
        rt_assert_f(!referrers->isTooSmall() || old_node->isAnchorListLocked(), 
            "invalid anchorList " PTR_DBG_SIG, PTR_DBG_INFO(old_node));
        
        debug_only(int cnt_anchor = 0;)
        for (ReverseIterator it(referrers); it.hasNext(); ) {
          ShortOOP* ptr = (ShortOOP*)it.next_ptr();
          adjust_anchor_pointer(ptr, old_node);
          debug_only(cnt_anchor++;)
        }
      }
    }
    else {
      adjust_anchor_pointer(&old_node->getSingleAnchor(), old_node);
    }
    old_node->unmarkDirtyReferrerPoints();
  }

  return size; 
}

class AdjustAnchorClosure {
  public:
  int _cnt_anchors;

  AdjustAnchorClosure() : _cnt_anchors(0) {}
  void do_oop(ShortOOP* ppAnchor) {
    if (ppAnchor->getOffset() != 0) {
      debug_only(_cnt_anchors++;)
      oop forwardee = cast_to_oop((GCObject*)*ppAnchor)->forwardee();
      if (forwardee != NULL) {
        *ppAnchor = to_obj(forwardee);
      }
    }
  }
};

void rtHeap::prepare_adjust_pointers(HeapWord* old_gen_heap_start) {
  g_adjust_pointer_closure._old_gen_start = old_gen_heap_start;
  rtgc_log(LOG_OPT(2), "old_gen_heap_start %p", old_gen_heap_start);
  rt_assert(MarkSweep::_resurrect_stack.is_empty());
  debug_only(g_cnt_ref_anchor = 0;)
  debug_only(g_cnt_alive_obj = 0;);
  debug_only(g_cnt_multi_anchored_obj = 0;)

    g_young_roots.resize(0);

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
    rtgc_debug_log(this, "garbage marking on %p(%s) rc=%d %s", this, getClassName(this), this->getRootRefCount(), reason);
  }
  rt_assert_f(!this->isGarbageMarked(),
      "already marked garbage %p(%s)", this, getClassName(this));
  rt_assert_f(this->getRootRefCount() <= (reason != NULL ? ZERO_ROOT_REF : ZERO_ROOT_REF + 1),
      "invalid garbage marking on %p(%s) rc=%d", this, getClassName(this), this->getRootRefCount());
  rt_assert_f(this->isTrackable() || !cast_to_oop(this)->is_gc_marked() || reason == NULL,
      "invalid garbage marking on %p(%s) rc=%d discovered=%p ghost=%d", this, getClassName(this), this->getRootRefCount(),
      __get_discovered(cast_to_oop(this)), rtHeapEx::print_ghost_anchors((GCObject*)this));

  _flags.isGarbage = true;
  // flags().isPublished = true;
}

bool rtHeapEx::print_ghost_anchors(GCObject* node, int depth) {
  if (depth > 0 && !rtHeap::is_alive(cast_to_oop(node)), false) return true;

  const int discovered_off = java_lang_ref_Reference::discovered_offset();
  AnchorIterator ai(node);
  rtgc_log(!ai.hasNext(), "no anchors for %p", node);

  while (ai.hasNext()) {
    if (node->hasSafeAnchor()) {
      GCObject* anchor = node->getSafeAnchor();
      bool isClass = cast_to_oop(anchor)->klass() == vmClasses::Class_klass();
      rtgc_log(1, "safe anchor[%d] %p(%s)[%d] unsafe:%d rc:%d gc_m:%d isClass=%d cldHolder=%p -> %p(%s) tr=%d", 
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
      rtgc_log(1, "ghost anchor[%d] %p(%s)[%d] unsafe:%d rc:%d gc_m:%d isClass=%d cldHolder=%p -> %p(%s) tr=%d", 
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


void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
#ifdef ASSERT  
  if (is_alive(p, false) || (node->isTrackable() ? !node->isUnreachable() : node->hasAnchor())) {
    rtHeapEx::print_ghost_anchors(to_obj(p));
  }
#endif

  rt_assert_f(is_destroyed(p), "wrong on garbage %p[%d](%s) unreachable=%d tr=%d rc=%d ac=%d isUnsafe=%d ghost=%d", 
        node, node->getShortcutId(), RTGC::getClassName(node), node->isUnreachable(),
        node->isTrackable(), node->getRootRefCount(), node->getAnchorCount(), node->isUnstableMarked(),
        rtHeapEx::print_ghost_anchors(to_obj(p)));
  rt_assert(node->isTrackable() ? node->isUnreachable() : node->getAnchorCount() == 0);
  return;
}

void rtHeap::finish_adjust_pointers() {
  rtgc_log(true, "alive objects %d anchors %d multi %d", g_cnt_alive_obj, g_cnt_ref_anchor, g_cnt_multi_anchored_obj);

  g_adjust_pointer_closure._old_gen_start = NULL;

  if (BATCH_UPDATE_ANCHORS) {
    AdjustAnchorClosure adjustAnchorClosure;
    ReferrerList::iterateAllAnchors(&adjustAnchorClosure);
    rtgc_log(true, "total anchors in the referrer-list: %d", adjustAnchorClosure._cnt_anchors);
  }

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
  rtgc_log(LOG_OPT(1), "prepare_rtgc");
  debug_only(cnt_resurrect = 0;)
  debug_only(g_cnt_skip_garbage = 0;)
  debug_only(g_marked_root_count = 0;)
  is_gc_started = true;
  rt_assert(g_stack_roots.size() == 0);
  if (rtHeap::useModifyFlag()) {
    UpdateLogBuffer::process_update_logs();
  }
  g_saved_young_root_count = g_young_roots.size();
  debug_only(g_cntScan ++);
  rtgc_log(true, "--- g_saved_young_root_count=%d", g_saved_young_root_count);

#if TRACE_UPDATE_LOG
  g_field_update_cnt = 0;
  g_inverse_graph_update_cnt = 0;
#endif
}

void rtHeap::init_reference_processor(ReferencePolicy* policy) {
  rt_assert(is_gc_started);
  rtgc_log(LOG_OPT(1), "init_reference_processor %p", policy);
  debug_only(g_marked_root_count = 0;)
  if (policy != NULL) {
    FreeMemStore::clearStore();
    if (RtLazyClearWeakHandle) {
      WeakProcessor::oops_do(&clear_weak_handle_ref);
    }
    in_full_gc = 1;
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
  g_saved_young_root_count = 0;
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

void rtHeap::mark_dead_space(oopDesc* deadObj) {
  GCObject* obj = to_obj(deadObj);
  rt_assert(!deadObj->is_gc_marked());
  if (obj->isTrackable_unsafe()) {
    rt_assert(obj->isGarbageMarked());
    rt_assert(obj->isDestroyed());
  } else {
    rt_assert(obj->isUnreachable());
    obj->markGarbage(NULL);
    obj->markDestroyed();
  }
}

void rtHeap::ensure_trackable_link(oopDesc* anchor, oopDesc* obj) {
  if (anchor != obj) {
    rt_assert_f(rtHeap::is_alive(obj), "must not a garbage " PTR_DBG_SIG, PTR_DBG_INFO(obj)); 
    rt_assert_f(to_obj(obj)->isAcyclic() || !to_obj(obj)->isTrackable() ||
       to_obj(obj)->hasReferrer(to_obj(anchor)), 
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
  return (hash & RTGC_NO_HASHCODE) == 0;
}

intptr_t RtHashLock::initHash(markWord mark) {
  GCNode* node = (GCNode*)&mark;
  if (node->hasMultiRef()) {
    releaseHash();
    int hash = node->getIdentityHashCode();
    if (!isCodeFixed(hash)) {
      _hash = hash;
      return 0;
    } 

    hash &= ~RTGC_NO_HASHCODE;
    return hash;
  }

  if (_hash == 0) {
    _hash = allocateHashSlot(node->mayHaveAnchor() ? &node->getSingleAnchor() : NULL);
    rt_assert(isCodeFixed(_hash));
  }
  return 0;
}

int RtHashLock::allocateHashSlot(ShortOOP* first) {
  RTGC::lock_heap();
  ReferrerList* refList = ReferrerList::allocate(true);
  int hash = ReferrerList::getIndex(refList);
  RTGC::unlock_heap();
  if (first == NULL) {
    fatal("not tested!");
    refList->initEmpty();
    rt_assert(refList->empty());
    rt_assert(refList->approximated_item_count() == 0);
  } else {
    refList->init(*first);
    rt_assert(refList->approximated_item_count() == 1);
  }
  return hash & ~RTGC_NO_HASHCODE;
}

intptr_t RtHashLock::hash() {
  rt_assert(_hash != 0);
  return (intptr_t)(_hash & ~RTGC_NO_HASHCODE);
}

void RtHashLock::consumeHash(intptr_t hash) { 
  if (this->hash() == hash) _hash = 0; 
}

void RtHashLock::releaseHash() {
  if (_hash == 0) return;
  if (isCodeFixed(_hash)) {
    RTGC::lock_heap();
    ReferrerList::deleteSingleChunkList(ReferrerList::getPointer(_hash));
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
      rtgc_debug_log(node, "oop_recycled_iterate %p", node);
      closure->do_object(cast_to_oop(node));
      rtgc_debug_log(node, ">> oop_recycled_iterate %p", node);
    }
  }
#ifdef ASSERT  
  g_debug_cnt_untracked = 0;
#endif
  g_recycled.resize(0); 
}

int cnt_init = 0;
void rtHeap__initialize() {
  rtgc_log(true, "trackable_heap_start = %p narrowOpp:base = %p",  GCNode::g_trackable_heap_start, CompressedOops::base());

  g_young_roots.initialize();
  g_stack_roots.initialize();
  g_recycled.initialize();
  rtCLDCleaner::initialize();
  UpdateLogBuffer::reset_gc_context();

}

