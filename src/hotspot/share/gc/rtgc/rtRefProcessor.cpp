#include "gc/rtgc/rtRefProcessor.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace rtHeapUtil;
using namespace RTGC;

int       g_cntMisRef = 0;
int       g_cntGarbageRef = 0;
#define DO_CROSS_CHECK_REF true
bool rtHeap::DoCrossCheck = DO_CROSS_CHECK_REF;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_REF, function);
}

template <ReferenceType refType>
class RtRefProcessor {
public:
#if DO_CROSS_CHECK_REF
  HugeArray<oop> _refs;
#endif
  oopDesc* _ref_q;
  oopDesc* _pending_q;
  const int _referent_off;
  const int _discovered_off;

  RtRefProcessor() : _referent_off(0), _discovered_off(0) { _ref_q = NULL; _pending_q = NULL; }

  void init() {
    *(int*)&_referent_off = java_lang_ref_Reference::referent_offset();
    *(int*)&_discovered_off = java_lang_ref_Reference::discovered_offset();
  }

  template <ReferenceType scanType>
  void process_references(OopClosure* keep_alive);

  template <bool is_full_gc>
  void add_pending_references(oopDesc* pending_head, oopDesc* pending_tail_acc);

  template <ReferenceType scanType>
  oopDesc* get_valid_forwardee(oopDesc* obj);

  template <ReferenceType scanType>
  oopDesc* get_valid_referent(oopDesc* ref);

  void remove_reference(oop ref_op, oop prev_ref_op, oop next_ref_op, bool is_alive) {
    if (is_alive) {
      if (refType != REF_FINAL) {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_op, _referent_off, oop(NULL));
      }
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_op, _discovered_off, oop(NULL));
    }
    if (prev_ref_op == NULL) {
      _ref_q = next_ref_op;
    } else {
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(prev_ref_op, _discovered_off, next_ref_op);        
    }
  }

  void break_reference_links();

  void adjust_ref_q_pointers();

  void link_pending_reference(oopDesc* anchor, oopDesc* link);

  static const char* reference_type_to_string(ReferenceType rt) {
    switch (rt) {
      case REF_NONE: return "None ref";
      case REF_OTHER: return "Other ref";
      case REF_SOFT: return "Soft ref";
      case REF_WEAK: return "Weak ref";
      case REF_FINAL: return "Final ref";
      case REF_PHANTOM: return "Phantom ref";
      default:
        ShouldNotReachHere();
      return NULL;
    }
  }
};

static RtRefProcessor<REF_SOFT>   g_softRefProcessor;
static RtRefProcessor<REF_WEAK>   g_weakRefProcessor;
static RtRefProcessor<REF_FINAL>   g_finalRefProcessor;
static RtRefProcessor<REF_PHANTOM> g_phantomRefProcessor;


template <ReferenceType refType>
template <ReferenceType scanType>
oopDesc* RtRefProcessor<refType>::get_valid_forwardee(oopDesc* obj) {
  precond(_referent_off != 0 && _discovered_off != 0);

  bool is_full_gc = scanType >= REF_OTHER;
  if (is_full_gc) {
    if (DO_CROSS_CHECK_REF) return obj->is_gc_marked() ? obj : NULL;

    GCObject* node = to_obj(obj);
    precond(node->isTrackable());
    if (refType == REF_SOFT || refType == REF_WEAK) {
      precond(node->isGarbageMarked());
      node->clearGarbageAnchors();
      if (!node->isUnreachable()) {
        node->unmarkGarbage();
        return obj;
      } else {
        _rtgc.g_pGarbageProcessor->getGarbageNodes()->push_back(node);
        return NULL;
      }
    }
    return node->isGarbageMarked() ? NULL : obj;
  } 

  if (to_obj(obj)->isTrackable()) {
    if (to_obj(obj)->isGarbageMarked()) return NULL;
    return obj;
  }

  if (!obj->is_gc_marked()) return NULL;
  postcond(!to_obj(obj)->isGarbageMarked());
  return obj->forwardee();
}

template <ReferenceType scanType>
bool is_weak_reachable(GCObject* obj) {
  Klass* klass = cast_to_oop(obj)->klass();
  if (klass->id() != InstanceRefKlassID) return false;
  return scanType != REF_WEAK 
      || ((InstanceRefKlass*)klass)->reference_type() == scanType;
}

template <ReferenceType scanType>
bool rtHeapEx::removeWeakAnchors(GCObject* node) {
  precond(!node->isGarbageMarked());
  assert(!node->isStrongRootReachable(), " not garbage %p(%s)\n", node, getClassName(node));
  if (node->hasMultiRef()) {
    ReferrerList* referrers = node->getReferrerList();
    ShortOOP* ppStart = referrers->adr_at(0);
    ShortOOP* ppEnd = ppStart + referrers->size();
    for (ShortOOP* ppAnchor = ppStart; ppAnchor < ppEnd; ppAnchor++) {
      ShortOOP sp = *ppAnchor;
      precond(!sp->isGarbageMarked());
      if (!is_weak_reachable<scanType>(sp)) {
        GCObject* anchor = sp;
        rtgc_log(anchor != NULL, "referent %p has strong anchor %p (gm=%d/%d)(%s) %d/%d\n", 
            node, anchor, cast_to_oop(anchor)->is_gc_marked(), anchor->isGarbageMarked(), 
            getClassName(anchor), anchor->hasReferrer(), anchor->hasMultiRef());
        anchor = anchor->getSingleAnchor();
        if (anchor != NULL && anchor->hasMultiRef()) {
          rtgc_log(anchor != NULL, "anchor-1 %p has strong anchor %p (gm=%d/%d)(%s) %d/%d\n", 
              node, anchor, cast_to_oop(anchor)->is_gc_marked(), anchor->isGarbageMarked(), 
              getClassName(anchor), anchor->hasReferrer(), anchor->hasMultiRef());
          ReferrerList* list = anchor->getReferrerList();
          for (int i = 0; i < list->size(); i ++) {
            GCObject* r = list->at(i);
            rtgc_log(r != NULL, "node %p has strong anchor %p (gm=%d/%d)(%s) %d/%d\n", 
                node, r, cast_to_oop(r)->is_gc_marked(), r->isGarbageMarked(), 
                getClassName(r), r->hasReferrer(), r->hasMultiRef());
          }
        }
        return false;
      }
      for (ShortOOP* ppPrev = ppStart; ppPrev < ppAnchor; ppPrev++) {
        // Weak/Soft Reference 내에 referent 를 가리키는 field 가 또 있는 경우의 처리.
        if (*ppPrev == sp) {
          fatal("WoW, this weird case is really happen!");
          if (!sp->isGarbageMarked()) {
            return false;
          }
        }
      }
    }
  } else {//if (node->hasReferrer()) {
    GCObject* anchor = node->getSingleAnchor();
    assert(is_weak_reachable<scanType>(anchor) || anchor->isGarbageMarked(),
        "invalid soft/weak referent %p [ref=%p(%s)]\n", node, anchor, getClassName(anchor));
  } 

  node->removeAnchorList();

  return true;
}

template <ReferenceType scanType>
bool rtHeapEx::clear_garbage_links_and_weak_anchors(GCObject* link, GCObject* garbageAnchor) {
    rtgc_debug_log(garbageAnchor, "clear_garbage_links_and_weak_anchors %p->%p\n", garbageAnchor, link);
    rtgc_log(LOG_OPT(4), "clear_garbage_links_and_weak_anchors %p->%p (g=%d)\n", 
        garbageAnchor, link, link->isGarbageMarked());    
    if (!link->removeMatchedReferrers(garbageAnchor)) {
        return false;
    }
    if (link->isDirtyReferrerPoints()) {
      precond(!link->isStrongRootReachable());
      precond(link->hasReferrer());
      if (rtHeapEx::removeWeakAnchors<scanType>(link)) {
          link->unmarkDirtyReferrerPoints();
          g_cntMisRef --;
          rtgc_log(true, "--g_cntMisRef %d\n", g_cntMisRef);
      }
    }
    if (link->isUnsafeTrackable()) {
        rtgc_log(true || LOG_OPT(14), "Add unsafe referent %p -> %p\n", garbageAnchor, link);
        return true;
    } 
    return false;
}



template <ReferenceType refType>
template <ReferenceType scanType>
oopDesc* RtRefProcessor<refType>::get_valid_referent(oopDesc* obj) {
  precond(_referent_off != 0 && _discovered_off != 0);

  GCObject* node = to_obj(obj);

  bool clear_weak_anchors;
  switch (scanType) {
  case REF_SOFT:
    clear_weak_anchors = (refType == REF_WEAK || refType == REF_SOFT);
    break;
  case REF_WEAK:
    clear_weak_anchors = (refType == REF_WEAK);
    break;
  case REF_OTHER:
    // precond(refType == REF_WEAK || refType == REF_SOFT);
    if (obj->is_gc_marked()) return obj;
    precond(!node->isStrongRootReachable());
    node->unmarkDirtyReferrerPoints();
    return node->hasReferrer() && !node->isGarbageMarked() ? obj : NULL;
  default:
    clear_weak_anchors = false;
  }

  if (!clear_weak_anchors) {
    return get_valid_forwardee<scanType>(obj);
  }

  bool FULL_RTGC = false;
  if (!FULL_RTGC && obj->is_gc_marked()) {
    return obj;
  }

  precond(!node->isStrongRootReachable());

  if (node->isGarbageMarked()) {
    // A refrent may referenced by mutil references.
    precond(node->isTrackable() && !obj->is_gc_marked());
    return NULL;
  }

  assert(node->hasReferrer(), "wrong referent on %p(%s)\n", node, RTGC::getClassName(node));
  if (node->isDirtyReferrerPoints()) {
    return node->hasReferrer() ? obj : NULL;
  }

  fatal("WWWWWooops");
  if (!rtHeapEx::removeWeakAnchors<scanType>(node)) {
    node->markDirtyReferrerPoints();
    return obj;
  }

  if (!node->isActiveFinalizerReachable()) {
    node->markGarbage("garbage referent");
    _rtgc.g_pGarbageProcessor->destroyObject(node, rtHeapEx::clear_garbage_links_and_weak_anchors<scanType>, true);
    _rtgc.g_pGarbageProcessor->collectGarbage(true);
  }
  return NULL;
}

template <ReferenceType refType>
template <ReferenceType scanType>
void RtRefProcessor<refType>::process_references(OopClosure* keep_alive) {
  precond(_referent_off != 0 && _discovered_off != 0);

#ifdef ASSERT  
  int cnt_garbage = 0;
  int cnt_ref = 0;
  int cnt_pending = 0;
  int cnt_cleared = 0;
  int cnt_alive = 0;
  const char* ref_type = reference_type_to_string(refType);
#endif
  const bool is_full_gc = scanType != REF_NONE;

  oop prev_acc_ref;
  oop next_ref_op;
  oop alive_head = NULL;
  oop alive_tail;
  oopDesc* pending_tail_acc = NULL;
  oopDesc* pending_head = NULL;
  for (oop ref_op = _ref_q; ref_op != NULL; ref_op = next_ref_op) {
    // rtgc_log(true || LOG_OPT(3), "check %s %p\n", ref_type, (void*)ref_op);
    next_ref_op = RawAccess<>::oop_load_at(ref_op, _discovered_off);
    precond(ref_op != next_ref_op);
    debug_only(cnt_ref++;)

    oop acc_ref = get_valid_forwardee<scanType>(ref_op);
    if (acc_ref == NULL) {
      // final reference 는 referent 보다 먼저 삭제될 수 없다.
      precond(refType != REF_FINAL);
      rtgc_log(LOG_OPT(3), "garbage %s %p removed\n", ref_type, (void*)ref_op);
      debug_only(cnt_garbage++;)
      continue;
    }
    precond(!to_obj(acc_ref)->isGarbageMarked());

    oop referent_op = RawAccess<>::oop_load_at(ref_op, _referent_off);
    assert(referent_op != NULL, "wrong ref %p -> %p\n", (void*)ref_op, (void*)acc_ref);
    if (refType <= REF_WEAK) {
      precond(referent_op != ref_op);
    }
    else if (referent_op == ref_op) {
      /**
       * Two step referent clear (to hide discoverd-link)
       * 참고) referent 값이 null 이 되면, discovered 가 normal-ref 참조로 처리된다.
       *      이에, reference-queue 에서 제거한 후, referent 값을 null 로 변경한다.
       * See java_lang_ref_Reference::clear_referent().
       */
      debug_only(cnt_cleared++;)
      precond(refType != REF_FINAL);
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, _referent_off, oop(NULL));
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, _discovered_off, oop(NULL));
      rtgc_log(LOG_OPT(3), "referent cleaned %p (maybe by PhantomCleanable)\n", (void*)acc_ref);
      continue;
    }

    oop acc_referent = get_valid_referent<scanType>(referent_op);
    if (refType <= REF_WEAK) {
      precond(referent_op != NULL);
    }
    else if (acc_referent == NULL) {
      debug_only(cnt_pending++;)
      if (refType == REF_FINAL) {
        acc_referent = referent_op;
        if (DO_CROSS_CHECK_REF || !is_full_gc) {
          keep_alive->do_oop((oop*)&acc_referent);
          postcond(referent_op->is_gc_marked());
          precond(!is_full_gc || acc_referent == referent_op);
        }
        GCObject* node = to_obj(acc_referent);
        precond(node->isActiveFinalizerReachable());
        node->unmarkActiveFinalizereReachable();
        postcond(node->getRootRefCount() == 0);
        if (to_obj(acc_ref)->isTrackable()) {
          RTGC::add_referrer_ex(acc_referent, acc_ref, true);
        }
      }
      if (!is_full_gc || refType != REF_FINAL) {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, _referent_off, acc_referent);
      }
      rtgc_log(LOG_OPT(3), "reference %p(->%p tr:%d) with garbage referent(%p) linked after (%p)\n", 
            (void*)ref_op, (void*)acc_ref, to_obj(acc_ref)->isTrackable(), (void*)referent_op, (void*)pending_tail_acc);

      if (pending_head == NULL) {
        precond(pending_tail_acc == NULL);
        pending_head = acc_ref;
      } else {
        link_pending_reference(pending_tail_acc, acc_ref);
      }
      pending_tail_acc = acc_ref;
    } else {
      java_lang_ref_Reference::set_discovered_raw(acc_ref, alive_head);
      alive_head = acc_ref;
      // if (alive_head == NULL) {
      //   alive_head = acc_ref;
      //   alive_tail = acc_ref;
      // } else if (alive_tail != prev_acc_ref) {
      //   java_lang_ref_Reference::set_discovered_raw(alive_tail, acc_ref);
      // }
      prev_acc_ref = acc_ref;
      debug_only(cnt_alive++;)

      if (scanType == REF_OTHER) {
        if (!acc_referent->is_gc_marked()) {
          //rtgc_log(true || LOG_OPT(3), "keepAlive referent of (%p->%p) marked %p -> %p\n", (void*)ref_op, (void*)acc_ref, (void*)referent_op, (void*)acc_referent);
          //keep_alive->do_oop((oop*)&acc_referent);
          assert(acc_referent->is_gc_marked(), "ref %p\n", (void*)acc_ref);
        }
      }
      rtgc_log(LOG_OPT(3), "referent of (%p->%p) marked %p -> %p\n", (void*)ref_op, (void*)acc_ref, (void*)referent_op, (void*)acc_referent);
      if (!is_full_gc) {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, _referent_off, acc_referent);
      } else {
        postcond(referent_op == acc_referent);
      }
    }   
  } 

  _ref_q = alive_head;
  if (alive_head != NULL) {

  }
  rtgc_log(LOG_OPT(3), "total %s scanned %d, garbage %d, cleared %d, pending %d, active %d q=%p\n",
        ref_type, cnt_ref, cnt_garbage, cnt_cleared, cnt_pending, cnt_alive, (void*)_ref_q);

  if (pending_head != NULL) {
    oopDesc* enqueued_top_np = Universe::swap_reference_pending_list(pending_head);
    link_pending_reference(pending_tail_acc, enqueued_top_np);
    // add_pending_references<is_full_gc>(pending_head, pending_tail_acc);
  }
}

template <ReferenceType refType>
void RtRefProcessor<refType>::link_pending_reference(oopDesc* anchor, oopDesc* link) {
  precond(_referent_off != 0 && _discovered_off != 0);

  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(anchor, _discovered_off, link);
  if (link != NULL && to_node(anchor)->isTrackable()) {
    precond(!to_obj(link)->isGarbageMarked());
    RTGC::add_referrer_ex(link, anchor, true);
  }
}


template <ReferenceType refType>
template <bool is_full_gc>
void RtRefProcessor<refType>::add_pending_references(oopDesc* pending_head, oopDesc* pending_tail_acc) {
  precond(_referent_off != 0 && _discovered_off != 0);

  oopDesc* enqueued_top_np = Universe::swap_reference_pending_list(pending_head);
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail_acc, _discovered_off, enqueued_top_np);
  if (enqueued_top_np != NULL && to_node(pending_tail_acc)->isTrackable()) {
    precond(!to_obj(enqueued_top_np)->isGarbageMarked());
    RTGC::add_referrer_ex(enqueued_top_np, pending_tail_acc, true);
  }
}


template <ReferenceType refType>
void RtRefProcessor<refType>::break_reference_links() {
  precond(_referent_off != 0 && _discovered_off != 0);
#if DO_CROSS_CHECK_REF
  int cntRef = _refs.size();
  for (int i = cntRef; --i >= 0; ) {
    oopDesc* ref_op = _refs.at(i);
#else
  oop next_ref_op;
  oop prev_ref_op = NULL;
  for (oop ref_op = _ref_q; ref_op != NULL; ref_op = next_ref_op) {
    next_ref_op = RawAccess<>::oop_load_at(ref_op, _discovered_off);
#endif    
    oopDesc* referent = ref_op->obj_field_access<AS_NO_KEEPALIVE>(_referent_off);
#if DO_CROSS_CHECK_REF
    if (referent == NULL) {
      _refs.removeFast(i);
      continue;
    } 
#else
    if (referent == ref_op) {
      remove_reference(ref_op, prev_ref_op, next_ref_op, true);
      continue;
    }
    prev_ref_op = ref_op;
#endif

    GCObject* referent_node = to_obj(referent);
    if (referent_node->getRootRefCount() > 0) continue;

    GCObject* ref_node = to_obj(ref_op);
    if (referent_node->isTrackable()) {
      if (!referent_node->isUnstableMarked()) {
        if (referent_node->hasShortcut() && referent_node->getSafeAnchor() == ref_node) {
          referent_node->getShortcut()->split(ref_node, referent_node);
        }
        _rtgc.g_pGarbageProcessor->addUnstable(referent_node);
        rtgc_log(false, "add unstable referent %p %d\n", referent, referent_node->getRootRefCount());
      }
    } else {
      rtgc_log(true, "yg referent %p\n", referent);
      // postcond(!cast_to_oop(node)->is_gc_marked())
    }
    ref_node->markGarbage();
  }
  rtgc_log(true, "invalidate_referent %d/%d\n", _refs.size(), cntRef);  
}

#if DO_CROSS_CHECK_REF
static void adjust_points(HugeArray<oop>& _refs, bool is_full_gc, bool resurrect_ref) {
  auto garbage_list = _rtgc.g_pGarbageProcessor->getGarbageNodes();
  int cntRef = _refs.size();
  for (int i = cntRef; --i >= 0; ) {
    oop ref_op = _refs.at(i);
    GCObject* node = to_obj(ref_op);
    if (resurrect_ref) {
      precond(node->isTrackable());
      node->clearGarbageAnchors();
      if (!node->isUnreachable()) {
        precond(ref_op->is_gc_marked());
        node->unmarkGarbage();
      } else {
        precond(!ref_op->is_gc_marked());
        garbage_list->push_back(node);
        _refs.removeFast(i);
        continue;
      }
    }
    else if (!rtHeap::is_alive(ref_op)) {
      _refs.removeFast(i);
      continue;
    }
    oopDesc* referent = java_lang_ref_Reference::unknown_referent_no_keepalive(ref_op);
    precond(referent != NULL);
    if (!rtHeap::is_alive(referent)) {
      _refs.removeFast(i);
      continue;
    }
    if (is_full_gc || !node->isTrackable()) {
      /* 첫 등록된 reference 는 copy_to_survival_space() 실행 전에는 trackable이 아니다.*/
      oop forwardee = ref_op->forwardee();
      _refs.at(i) = (is_full_gc && forwardee == NULL) ? ref_op : forwardee;
    }
  }
  if (resurrect_ref) {
    _rtgc.g_pGarbageProcessor->collectGarbage(true);    
  }
  rtgc_log(true, "adjust_points %d/%d\n", _refs.size(), cntRef);
}
#endif

template <ReferenceType refType>
void RtRefProcessor<refType>::adjust_ref_q_pointers() {
  const char* ref_type = reference_type_to_string(refType);
  oopDesc* prev_ref_op = NULL;
  oop next_ref_op;
  for (oop ref_op = _ref_q; ref_op != NULL; ref_op = next_ref_op) {
    next_ref_op = RawAccess<>::oop_load_at(ref_op, _discovered_off);

    oop acc_ref = ref_op->forwardee();
    rtgc_log(LOG_OPT(3), "adust_pointer ref %s %p->%p\n", ref_type, (void*)ref_op, (void*)acc_ref);
    if (acc_ref == NULL) acc_ref = ref_op;
    precond(ref_op != next_ref_op);

    if (true || refType >= REF_FINAL) {
      oop referent_op = RawAccess<>::oop_load_at(ref_op, _referent_off);
      rtgc_log(LOG_OPT(3), "adust_pointer referent %s %p->(%p)\n", ref_type, (void*)acc_ref, (void*)referent_op);
      oop acc_referent = referent_op->forwardee();
      if (acc_referent == NULL) acc_referent = referent_op;
      if (referent_op != acc_referent) {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_op, _referent_off, acc_referent);
      }
    } else {
      // WEAK 와 SOFT referent 는 이미 새 주소를 가지고 있다. (복사는 되지 않은 상태)
    }

    if (prev_ref_op != NULL) {
      java_lang_ref_Reference::set_discovered_raw(prev_ref_op, acc_ref);
    } else {
      _ref_q = acc_ref;
    }
    prev_ref_op = ref_op;
  } 
}




void rtHeap::link_discovered_pending_reference(oopDesc* ref_q, oopDesc* end) {
  rtgc_log(LOG_OPT(3), "link_discovered_pending_reference from %p to %p\n", (void*)ref_q, end);
  oopDesc* discovered;
  for (oopDesc* obj = ref_q; obj != end; obj = discovered) {
    discovered = java_lang_ref_Reference::discovered(obj);
    if (to_obj(obj)->isTrackable()) {
      RTGC::add_referrer_ex(discovered, obj, true);
    }
  }
}

template <ReferenceType clear_ref, bool clear_weak_soft>
void __process_java_references(OopClosure* keep_alive, VoidClosure* complete_gc) {
  g_weakRefProcessor.init();
  g_softRefProcessor.init();
  g_finalRefProcessor.init();
  g_phantomRefProcessor.init();
  assert(g_cntMisRef == 0, "g_cntMisRef %d\n", g_cntMisRef);
  bool is_full_gc = clear_ref != REF_NONE;
  if (clear_weak_soft) {
    g_weakRefProcessor.process_references<clear_ref>(keep_alive);
    g_softRefProcessor.process_references<clear_ref>(keep_alive);
    if (clear_ref >= REF_SOFT) {
      _rtgc.g_pGarbageProcessor->collectGarbage(true);
      g_weakRefProcessor.process_references<REF_OTHER>(keep_alive);
      g_softRefProcessor.process_references<REF_OTHER>(keep_alive);
    }
    complete_gc->do_void();
  } else {
    g_finalRefProcessor.process_references<clear_ref>(keep_alive);
    complete_gc->do_void();
    g_phantomRefProcessor.process_references<clear_ref>(NULL);
  }
}



void rtHeap__clear_garbage_young_roots(bool is_full_gc);

void rtHeap::process_weak_soft_references(OopClosure* keep_alive, VoidClosure* complete_gc, ReferenceType clear_ref) {
  // const char* ref_type = reference_type_to_string(clear_ref);
  rtgc_log(false, "process_weak_soft_references %d\n", clear_ref);
  switch (clear_ref) {
    case REF_NONE:
      __process_java_references<REF_NONE, true>(keep_alive, complete_gc);
      break;
    case REF_SOFT:
      __process_java_references<REF_SOFT, true>(keep_alive, complete_gc);
      g_softRefProcessor.break_reference_links();
      g_weakRefProcessor.break_reference_links();
      #if DO_CROSS_CHECK_REF
        rtHeap__clear_garbage_young_roots(true);
        adjust_points(g_softRefProcessor._refs, true, true);
        adjust_points(g_weakRefProcessor._refs, true, true);
      #else
      #endif
      break;
    case REF_WEAK:
      __process_java_references<REF_WEAK, true>(keep_alive, complete_gc);
      g_weakRefProcessor.break_reference_links();
      #if DO_CROSS_CHECK_REF
        rtHeap__clear_garbage_young_roots(true);
        adjust_points(g_weakRefProcessor._refs, true, true);
      #endif
      break;
    default:
      fatal("invalid clear_ref type: %d\n", clear_ref);
  }
}

void rtHeap::process_final_phantom_references(VoidClosure* complete_gc, bool is_tenure_gc) {
  if (is_tenure_gc) {
    __process_java_references<REF_OTHER, false>(NULL, complete_gc);
  } else {
    __process_java_references<REF_NONE, false>(NULL, complete_gc);
  }
}

bool rtHeap::is_active_finalizer_reachable(oopDesc* final_referent) {
  return to_obj(final_referent)->isActiveFinalizerReachable();
}

void rtHeapEx::adjust_ref_q_pointers(bool is_full_gc) {
  #if DO_CROSS_CHECK_REF
    adjust_points(g_softRefProcessor._refs, is_full_gc, false);
    adjust_points(g_weakRefProcessor._refs, is_full_gc, false);
  #endif
  if (is_full_gc) {
    g_softRefProcessor.adjust_ref_q_pointers();
    g_weakRefProcessor.adjust_ref_q_pointers();
    g_finalRefProcessor.adjust_ref_q_pointers();
    g_phantomRefProcessor.adjust_ref_q_pointers();
  } else {
  }
}

void rtHeap__ensure_garbage_referent(oopDesc* ref, oopDesc* referent, bool clear_soft) {
  precond(rtHeap::DoCrossCheck);
  precond(!java_lang_ref_Reference::is_phantom(ref));
  GCObject* node = to_obj(referent);
  g_cntGarbageRef ++;
  if (java_lang_ref_Reference::is_final(ref)) {
    precond(referent->is_gc_marked());
    precond(!node->isGarbageMarked());
    precond(!node->hasReferrer());
    precond(!node->isStrongRootReachable());
    precond(node->getRootRefCount() > ZERO_ROOT_REF);
    node->unmarkActiveFinalizereReachable();
    precond(node->isUnreachable());
    if (node->isTrackable()) {
      RTGC::add_referrer_ex(referent, ref, true);
    }
    return;
  }
  if (!rtHeap::is_alive(referent)) return;

  g_cntMisRef ++;
  rtgc_log(true, "++g_cntMisRef %p tr=%d, %d/%d\n", node, node->isTrackable(), g_cntMisRef, g_cntGarbageRef);
}

static void __validate_trackable_refs(HugeArray<oop> _refs) {
  int cntTrackable = 0;
  for (int i = _refs.size(); --i >= 0; ) {
    GCObject* node = to_obj(_refs.at(i));
    if (node->isTrackable()) cntTrackable ++;
  }
  assert(cntTrackable == _refs.size(), " cntTrackable %d/%d\n", cntTrackable, _refs.size());
}

void rtHeapEx::validate_trackable_refs() {
  __validate_trackable_refs(g_softRefProcessor._refs);
  __validate_trackable_refs(g_weakRefProcessor._refs);
}

void rtHeap::init_java_reference(oopDesc* ref, oopDesc* referent) {
  precond(RtNoDiscoverPhantom);
  precond(referent != NULL);

  ptrdiff_t referent_offset = java_lang_ref_Reference::referent_offset();
  ReferenceType ref_type = InstanceKlass::cast(ref->klass())->reference_type();
  oopDesc** ref_q;
  switch (ref_type) {
    case REF_PHANTOM:
      ref_q = &g_phantomRefProcessor._ref_q;
      rtgc_log(LOG_OPT(3), "created Phantom ref %p for %p\n", (void*)ref, referent);
      break;

    case REF_FINAL:
      to_obj(referent)->markActiveFinalizereReachable();
      ref_q = &g_finalRefProcessor._ref_q;
      rtgc_log(LOG_OPT(3), "created Final ref %p for %p\n", (void*)ref, referent);
      #if DO_CROSS_CHECK_REF
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, referent_offset, referent);
        return;
      #endif
      break;

    default:
      // TODO -> age 변경 시점을 늦춘다(?).
      ref->set_mark(ref->mark().set_age(markWord::max_age));
      ref_q = ref_type == REF_WEAK ? &g_weakRefProcessor._ref_q : &g_softRefProcessor._ref_q;
      rtgc_log(false, "weak=%d/soft ref %p -> %p age = %d\n", 
          ref_type == REF_WEAK, (void*)ref, referent, ref->age());
      #if DO_CROSS_CHECK_REF
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, referent_offset, referent);
        if (ref_type == REF_WEAK) {
          g_weakRefProcessor._refs.push_back(ref);
        } else {
          g_softRefProcessor._refs.push_back(ref);
        }
        return;
      #endif
      break;
  }

  precond(!to_obj(ref)->isTrackable());
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, referent_offset, referent);

  oop next_discovered = Atomic::xchg(ref_q, ref);
  java_lang_ref_Reference::set_discovered_raw(ref, next_discovered);
  return;
}

