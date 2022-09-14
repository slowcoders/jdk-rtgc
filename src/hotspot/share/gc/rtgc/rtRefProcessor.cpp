#include "gc/rtgc/rtRefProcessor.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "gc/serial/serialGcRefProcProxyTask.hpp"

using namespace rtHeapUtil;
using namespace RTGC;

#define DO_CROSS_CHECK_REF true
#if DO_CROSS_CHECK_REF
static int g_cntMisRef = 0;
static int g_cntGarbageRef = 0;
static int g_cntCleanRef = 0;
#endif
bool rtHeap::DoCrossCheck = DO_CROSS_CHECK_REF;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_REF, function);
}

HugeArray<oop> g_garbage_referents;
static ReferenceType __getRefType(oop ref_p) {
  return InstanceKlass::cast(ref_p->klass())->reference_type();
}

class RefListBase {
public:
#if DO_CROSS_CHECK_REF
  HugeArray<oop> _refs;
#endif
  oopDesc* _ref_q;
  static int _referent_off;
  static int _discovered_off;
  static oop g_pending_head;
  static oop g_pending_tail;

  RefListBase() { 
    _ref_q = NULL; 
  }

  virtual ReferenceType ref_type() = 0;

  static void link_pending_reference(oop anchor, oop link) {
    // rtgc_log(true, "link_pending_reference %p -> %p\n", (void*)anchor, (void*)link);
    HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(anchor, RefListBase::_discovered_off, link);
    precond(link == RawAccess<>::oop_load_at(anchor, RefListBase::_discovered_off));
    if (link != NULL && to_node(anchor)->isTrackable()) {
      precond(!to_obj(link)->isGarbageMarked());
      RTGC::add_referrer_ex(link, anchor, true);
    }
  }

  static void enqueue_pending_ref(oop ref_p) {
    if (g_pending_head == NULL) {
      g_pending_head = g_pending_tail = ref_p;
    } else {
      link_pending_reference(ref_p, g_pending_head);
      g_pending_head = ref_p;
    }
  }

  static void flush_penging_list() {
    if (g_pending_head != NULL) {
      oop enqueued_top_np = Universe::swap_reference_pending_list(g_pending_head);
      link_pending_reference(g_pending_tail, enqueued_top_np);
  #ifdef ASSERT
      for (oop ref_p = g_pending_head; ref_p != enqueued_top_np; ref_p = RawAccess<>::oop_load_at(ref_p, RefListBase::_discovered_off)) {
        oop r = RawAccess<>::oop_load_at(ref_p, RefListBase::_referent_off);
        // rtgc_log(true, "ref %p(%s) %p", (void*)ref_p, getClassName(to_obj(ref_p)), (void*)r);
        assert(r == NULL || InstanceKlass::cast(ref_p->klass())->reference_type() == REF_FINAL, "ref %p(%s)", (void*)ref_p, getClassName(to_obj(ref_p)));
      }
  #endif
      g_pending_head = NULL;
    }
  }
};

template <ReferenceType refType>
class RtRefList : public RefListBase {
public:
  // template <ReferenceType scanType>
  // void process_references(OopClosure* keep_alive);

  // template <bool is_full_gc>
  // void add_pending_references(oopDesc* pending_head, oopDesc* pending_tail_acc);

  template <ReferenceType scanType>
  oopDesc* get_valid_forwardee(oopDesc* obj);

  // template <ReferenceType scanType>
  // oopDesc* get_valid_referent(oopDesc* ref);

  ReferenceType ref_type() { return refType; }

  // void remove_reference(oop ref_op, oop prev_ref_op, oop next_ref_op, bool is_alive) {
  //   fatal("not checked");
  //   if (is_alive) {
  //     if (refType != REF_FINAL) {
  //       HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_op, _referent_off, oop(NULL));
  //     }
  //     HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_op, _discovered_off, oop(NULL));
  //   }
  //   if (prev_ref_op == NULL) {
  //     _ref_q = next_ref_op;
  //   } else {
  //     HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(prev_ref_op, _discovered_off, next_ref_op);        
  //   }
  // }

  void break_reference_links(ReferencePolicy* policy);

  // void link_pending_reference(oopDesc* anchor, oopDesc* link);
};

oop RefListBase::g_pending_head = NULL;
oop RefListBase::g_pending_tail;
int RefListBase::_referent_off;
int RefListBase::_discovered_off;



class RefIterator  {

  RefListBase& _refList;
  int _discovered_off;
  int _referent_off;

  bool do_cross_test;
  int _idx;
  oop _curr_ref;
  oop _next_ref;
  oop _prev_ref_op;
  oop _referent_p;
  bool _ref_removed;

public:  
  RefIterator(RefListBase &refProcessor) : _refList(refProcessor) {
    _next_ref = refProcessor._ref_q;
    do_cross_test = _next_ref == NULL;

    _curr_ref = _prev_ref_op = NULL;
    _discovered_off = refProcessor._discovered_off;
    _referent_off = refProcessor._referent_off;
    _referent_p = NULL;
    _ref_removed = false;
    _idx = refProcessor._refs.size();
  }

  oopDesc* ref() {
    return _curr_ref;
  }

  oopDesc* referent_p() {
    return _referent_p;
  }

  HeapWord* referent_addr() {
    return java_lang_ref_Reference::referent_addr_raw(_curr_ref);
  }

  oopDesc* get_raw_referent_p() {
    oop p = RawAccess<>::oop_load_at(_curr_ref, _referent_off);
    return p;
  }

  template <bool skipGarbageRef = true, bool skipClearedRef = true>
  GCObject* next_ref() {
    while (true) {
      if (do_cross_test) {
        if (--_idx < 0) return NULL;
        _curr_ref = _refList._refs.at(_idx);
      } else {
        if (_ref_removed && _prev_ref_op != _curr_ref) {
          _ref_removed = false;
          if (_prev_ref_op == NULL) {
            precond(_refList._ref_q == _curr_ref);
          } else {
            precond(RawAccess<>::oop_load_at(_prev_ref_op, _discovered_off) == _curr_ref);        
          }
        }
        _prev_ref_op = _curr_ref;
        _curr_ref = _next_ref;
        if (_curr_ref == NULL) return NULL;
        _next_ref = RawAccess<>::oop_load_at(_curr_ref, _discovered_off);
      }
      precond((void*)_curr_ref > (void*)0x30);

      if (!skipGarbageRef) {
        precond(rtHeap::is_alive(_curr_ref));
      } else if (!rtHeap::is_alive(_curr_ref)) {
        assert(!do_cross_test || !_curr_ref->is_gc_marked() || rtHeapUtil::is_dead_space(_curr_ref), 
            "invalid gargabe %p(%s) tr=%d, rc=%d ghost=%d\n", 
            (void*)_curr_ref, RTGC::getClassName(to_obj(_curr_ref)),
            to_obj(_curr_ref)->isTrackable(), to_obj(_curr_ref)->getRootRefCount(),
            rtHeapEx::print_ghost_anchors(to_obj(_curr_ref)));
        this->remove_curr_ref();
        continue;
      }
      precond(__getRefType(_curr_ref) == _refList.ref_type());

      _referent_p = get_raw_referent_p();
      assert(do_cross_test || _referent_p != NULL, "null referent ref %p\n", (void*)_curr_ref);

      if (!skipClearedRef) {
        postcond(_referent_p != (do_cross_test ? oop(NULL) : _curr_ref));
      }
      else if (_referent_p == (do_cross_test ? oop(NULL) : _curr_ref)) {
        clear_referent();
        clear_discovered();
        this->remove_curr_ref();
        continue;
      }

      precond((void*)_curr_ref > (void*)32);
      return to_obj(_curr_ref);
    }
  }

  template <bool is_full_gc>
  oop get_valid_forwardee(oop old_p) {
    return (!is_full_gc && to_obj(old_p)->isTrackable()) ? NULL :  old_p->forwardee();
  }

  template <bool is_full_gc>
  void adjust_ref_pointer() {
    precond(!rtHeap::DoCrossCheck || _curr_ref->is_gc_marked() || (!rtHeap::in_full_gc && to_obj(_curr_ref)->isTrackable()));
    oop new_p = get_valid_forwardee<is_full_gc>(_curr_ref);
    if (new_p != NULL) {
      precond((void*)new_p > (void*)32);
      if (do_cross_test) {
        _refList._refs.at(_idx) = new_p;
      }
      else if (_prev_ref_op == NULL) {
        _refList._ref_q = new_p;
      } else {
        RawAccess<>::oop_store_at(_prev_ref_op, _discovered_off, new_p);
      }
      // rtgc_log(true, "X) phantom ref moved %p -> %p\n", (void*)_curr_ref, (void*)new_p);
      if (!is_full_gc) {
        _curr_ref = new_p;
      } else {
        // 객체 복사가 되지 않은 상태.
      }
    } else {
      postcond(is_full_gc || to_obj(_curr_ref)->isTrackable());
    }
  }

  template <bool is_full_gc>
  void adjust_referent_pointer() {
    if (do_cross_test) return;
    
    oop old_p = _referent_p;
    oop new_p = get_valid_forwardee<is_full_gc>(old_p);
    if (new_p == NULL) {
      postcond(is_full_gc || to_obj(old_p)->isTrackable());
    } else if (new_p != old_p) {
      precond((void*)new_p > (void*)0x30);
      // rtgc_log(true, "move referent %p -> %p\n", (void*)old_p, (void*)new_p);
      RawAccess<>::oop_store_at(_curr_ref, _referent_off, new_p);
      if (!is_full_gc) {
        _referent_p = new_p;
      }
    }
  }

  void enqueue_curr_ref(bool clear_referent_p = true) {
    if (clear_referent_p) {
      clear_referent();
    }
    oop ref_p = _curr_ref;
    remove_curr_ref();
    
    if (do_cross_test) return;
    RefListBase::enqueue_pending_ref(ref_p);
  }

  void remove_curr_ref() {
    if (do_cross_test) {
      _refList._refs.removeFast(_idx);
      rtgc_log(false, "remove ref %p %d/%d\n", (void*)_curr_ref, _idx, _refList._refs.size());
    } else {
      if (_prev_ref_op == NULL) {
        _refList._ref_q = _next_ref;
      } else {
        RawAccess<>::oop_store_at(_prev_ref_op, _discovered_off, _next_ref);
      }
      _ref_removed = true;
      _curr_ref = _prev_ref_op;
    }
  }

  void break_weak_soft_link() {
    GCObject* ref = to_obj(_curr_ref);
    GCObject* referent = to_obj(this->_referent_p);
    if (referent->hasShortcut() && referent->getSafeAnchor() == ref) {
      referent->getShortcut()->split(ref, referent);
    }
  } 

  void clear_weak_soft_garbage_referent() {
    GCObject* referent = to_obj(_referent_p);
    if (referent->isTrackable()) {
      _rtgc.g_pGarbageProcessor->detectGarbage(referent, true);
    }
    if (!rtHeap::is_alive(_referent_p)) {
      precond(!do_cross_test || !_referent_p->is_gc_marked());
      referent->clearGarbageAnchors();
      enqueue_curr_ref();
    }
  }

private:  
  void clear_discovered() {
    if (do_cross_test) return;
    RawAccess<>::oop_store_at(_curr_ref, _discovered_off, oop(NULL));
  }

  void clear_referent() {
    if (do_cross_test) return;
    // rtgc_log(true, "clear referent ref %p\n", (void*)_curr_ref);
    RawAccess<>::oop_store_at(_curr_ref, _referent_off, oop(NULL));
  }

};

static RtRefList<REF_SOFT>   g_softList;
static RtRefList<REF_WEAK>   g_weakList;
static RtRefList<REF_FINAL>   g_finalList;
static RtRefList<REF_PHANTOM> g_phantomList;

void rtHeapEx::initializeRefProcessor() {
  RefListBase::_referent_off = java_lang_ref_Reference::referent_offset();
  RefListBase::_discovered_off = java_lang_ref_Reference::discovered_offset();
  postcond(RefListBase::_referent_off != 0);
  postcond(RefListBase::_discovered_off != 0);
}

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

jlong rtHeapEx::_soft_ref_timestamp_clock;

jlong __get_soft_ref_timestamp_clock() {
  return rtHeapEx::_soft_ref_timestamp_clock;
}

void rtHeapEx::update_soft_ref_master_clock() {
  // Update (advance) the soft ref master clock field. This must be done
  // after processing the soft ref list.

  // We need a monotonically non-decreasing time in ms but
  // os::javaTimeMillis() does not guarantee monotonicity.
  jlong now = os::javaTimeNanos() / NANOSECS_PER_MILLISEC;
  jlong soft_ref_clock = java_lang_ref_SoftReference::clock();
  assert(soft_ref_clock == _soft_ref_timestamp_clock, "soft ref clocks out of sync");

  NOT_PRODUCT(
  if (now < _soft_ref_timestamp_clock) {
    log_warning(gc)("time warp: " JLONG_FORMAT " to " JLONG_FORMAT,
                    _soft_ref_timestamp_clock, now);
  }
  )
  // The values of now and _soft_ref_timestamp_clock are set using
  // javaTimeNanos(), which is guaranteed to be monotonically
  // non-decreasing provided the underlying platform provides such
  // a time source (and it is bug free).
  // In product mode, however, protect ourselves from non-monotonicity.
  if (now > _soft_ref_timestamp_clock) {
    _soft_ref_timestamp_clock = now;
    java_lang_ref_SoftReference::set_clock(now);
  }
  // Else leave clock stalled at its old value until time progresses
  // past clock value.
}

template <ReferenceType refType>
template <ReferenceType scanType>
oopDesc* RtRefList<refType>::get_valid_forwardee(oopDesc* obj) {
  precond(_referent_off != 0 && _discovered_off != 0);

  bool is_full_gc = scanType >= REF_OTHER;
  if (is_full_gc) {
    if (DO_CROSS_CHECK_REF) {
      if (obj->is_gc_marked()) {
        precond(!to_obj(obj)->isGarbageMarked());
        return obj;
      }
      return NULL;
    }

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
    if (to_obj(obj)->isGarbageMarked()) {
      precond(!obj->is_gc_marked());
      return NULL;
    }
    return obj;
  }

  if (!obj->is_gc_marked()) return NULL;
  postcond(!to_obj(obj)->isGarbageMarked());
  return obj->forwardee();
}


// template <ReferenceType refType>
// template <ReferenceType scanType>
// oopDesc* RtRefList<refType>::get_valid_referent(oopDesc* obj) {
//   precond(_referent_off != 0 && _discovered_off != 0);

//   GCObject* node = to_obj(obj);

//   bool clear_weak_anchors;
//   switch (scanType) {
//   case REF_SOFT:
//     clear_weak_anchors = (refType == REF_WEAK || refType == REF_SOFT);
//     break;
//   case REF_WEAK:
//     clear_weak_anchors = (refType == REF_WEAK);
//     break;
//   case REF_OTHER:
//     // precond(refType == REF_WEAK || refType == REF_SOFT);
//     if (obj->is_gc_marked()) return obj;
//     precond(!node->isStrongRootReachable());
//     node->unmarkDirtyReferrerPoints();
//     return node->hasReferrer() && !node->isGarbageMarked() ? obj : NULL;
//   default:
//     clear_weak_anchors = false;
//   }

//   if (!clear_weak_anchors) {
//     return get_valid_forwardee<scanType>(obj);
//   }

//   bool FULL_RTGC = false;
//   if (!FULL_RTGC && obj->is_gc_marked()) {
//     return obj;
//   }

//   precond(!node->isStrongRootReachable());

//   if (node->isGarbageMarked()) {
//     // A refrent may referenced by mutil references.
//     precond(node->isTrackable() && !obj->is_gc_marked());
//     return NULL;
//   }

//   assert(node->hasReferrer(), "wrong referent on %p(%s)\n", node, RTGC::getClassName(node));
//   if (node->isDirtyReferrerPoints()) {
//     return node->hasReferrer() ? obj : NULL;
//   }

//   fatal("WWWWWooops");
//   // if (!rtHeapEx::removeWeakAnchors<scanType>(node)) {
//   //   node->markDirtyReferrerPoints();
//   //   return obj;
//   // }

//   // if (!node->isActiveFinalizerReachable()) {
//   //   node->markGarbage("garbage referent");
//   //   _rtgc.g_pGarbageProcessor->destroyObject(node, rtHeapEx::clear_garbage_links_and_weak_anchors<scanType>, true);
//   //   _rtgc.g_pGarbageProcessor->collectGarbage(true);
//   // }
//   return NULL;
// }


// template <ReferenceType refType>
// template <bool is_full_gc>
// void RtRefList<refType>::add_pending_references(oopDesc* pending_head, oopDesc* pending_tail_acc) {
//   precond(_referent_off != 0 && _discovered_off != 0);

//   oopDesc* enqueued_top_np = Universe::swap_reference_pending_list(pending_head);
//   HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail_acc, _discovered_off, enqueued_top_np);
//   if (enqueued_top_np != NULL && to_node(pending_tail_acc)->isTrackable()) {
//     precond(!to_obj(enqueued_top_np)->isGarbageMarked());
//     RTGC::add_referrer_ex(enqueued_top_np, pending_tail_acc, true);
//   }
// }

static const bool REMOVE_REF_LINK = true;
template <ReferenceType refType>
void RtRefList<refType>::break_reference_links(ReferencePolicy* policy) {
  precond(_referent_off != 0 && _discovered_off != 0);
#if DO_CROSS_CHECK_REF
  int cntRef = _refs->size();
  for (int i = cntRef; --i >= 0; ) {
    oopDesc* ref_op = _refs->at(i);
#else
  oop next_ref_op;
  oop prev_ref_op = NULL;
  for (oop ref_op = _ref_q; ref_op != NULL; ref_op = next_ref_op) {
    next_ref_op = RawAccess<>::oop_load_at(ref_op, _discovered_off);
#endif    
    oopDesc* referent = ref_op->obj_field_access<AS_NO_KEEPALIVE>(_referent_off);
#if DO_CROSS_CHECK_REF
    if (referent == NULL) {
      _refs->removeFast(i);
      continue;
    } 
#else
    if (referent == ref_op) {
      remove_reference(ref_op, prev_ref_op, next_ref_op, true);
      continue;
    }
    prev_ref_op = ref_op;
#endif

    if (refType == REF_SOFT) {
      if (!policy->should_clear_reference(ref_op, rtHeapEx::_soft_ref_timestamp_clock)) {
        continue;   
      }
      rtgc_debug_log(referent, "clear soft ref %p -> %p(%d)\n", ref_op, referent, to_obj(referent)->getRootRefCount());
    }

    GCObject* referent_node = to_obj(referent);
    precond(!referent_node->isGarbageMarked());
    // rtgc_log(refType == REF_WEAK && g_cntCleanRef == 5, 
    //     "weak ref %p -> %p(%d)\n", ref_op, referent, referent_node->getRootRefCount());
    if (referent_node->getRootRefCount() > 0) {
      continue;
    }

    GCObject* ref_node = to_obj(ref_op);
    precond(ref_node->isTrackable());
    precond(!ref_node->isGarbageMarked());
    GCRuntime::disconnectReferenceLink(referent_node, ref_node);
    ref_node->markDirtyReferrerPoints();
  }
  rtgc_log(LOG_OPT(3), "break_reference_links %d/%d\n", _refs->size(), cntRef);  
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

// template <ReferenceType clear_ref, bool clear_weak_soft>
// void __process_java_references(OopClosure* keep_alive, VoidClosure* complete_gc) {
//   bool is_full_gc = clear_ref != REF_NONE;
//   if (clear_weak_soft) {
//     g_weakList.process_references<clear_ref>(keep_alive);
//     g_softList.process_references<clear_ref>(keep_alive);
//     if (clear_ref >= REF_SOFT) {
//       _rtgc.g_pGarbageProcessor->collectGarbage(true);
//       g_weakList.process_references<REF_OTHER>(keep_alive);
//       g_softList.process_references<REF_OTHER>(keep_alive);
//     }
//     complete_gc->do_void();
//   } else {
//     g_finalList.process_references<clear_ref>(keep_alive);
//     complete_gc->do_void();
//     g_phantomList.process_references<clear_ref>(NULL);
//   }
// }



void rtHeap__clear_garbage_young_roots(bool is_full_gc);

template<typename T, bool is_full_gc>
static void __keep_alive_final_referents(OopClosure* keep_alive, VoidClosure* complete_gc) {
  GCObject* ref;
  if (is_full_gc) {
    rtgc_log(true, "final q %p\n", g_finalList._ref_q);
    for (RefIterator iter(g_finalList); (ref = iter.next_ref<false, false>()) != NULL; ) {
      GCObject* referent = to_obj(iter.referent_p());
      if ((rtHeap::DoCrossCheck || !referent->isTrackable()) && !cast_to_oop(referent)->is_gc_marked()) {
        // resurrect
        rtgc_log(true, "resurrect final ref %p\n", referent);
        keep_alive->do_oop((T*)iter.referent_addr());
        if (ref->isTrackable()) {
          RTGC::add_referrer_ex(iter.referent_p(), iter.ref(), true);
        }
        iter.enqueue_curr_ref(false);
      } else {
        referent->markActiveFinalizereReachable();
      }
    }
  }

  if (!rtHeap::DoCrossCheck) {
    for (RefIterator iter(g_finalList); (ref = iter.next_ref<>()) != NULL; ) {
      oop referent_p = iter.referent_p();
      if (!to_obj(referent_p)->isTrackable() && !referent_p->is_gc_marked()) {
        keep_alive->do_oop((T*)iter.referent_addr());
      }
    }
  }

  complete_gc->do_void();
  rtHeap__clear_garbage_young_roots(is_full_gc);

  for (RefIterator iter(g_finalList); (ref = iter.next_ref<false, false>()) != NULL; ) {
    GCObject* referent = to_obj(iter.referent_p());
    if (!is_full_gc) {
      iter.adjust_ref_pointer<is_full_gc>();
      postcond((void*)referent == iter.get_raw_referent_p());
    }
    if (referent->isTrackable() ? !referent->isStrongReachable() : !cast_to_oop(referent)->is_gc_marked()) {
      referent->unmarkActiveFinalizereReachable();
      postcond(referent->isUnreachable());
      if ((rtHeap::DoCrossCheck || !referent->isTrackable()) && !cast_to_oop(referent)->is_gc_marked()) {
        keep_alive->do_oop((T*)iter.referent_addr());
      }
      rtgc_log(true, "final ref cleared 1 %p(%p) -> %p(%p)\n", (void*)ref, iter.ref(), referent, iter.get_raw_referent_p());
      if (to_obj(iter.ref())->isTrackable()) {
        RTGC::add_referrer_ex(iter.get_raw_referent_p(), iter.ref(), true);
      }
      iter.enqueue_curr_ref(false);
    } else {
      if (!is_full_gc) {
        iter.adjust_referent_pointer<is_full_gc>();
      }
      rtgc_log(true, "final ref not cleared %p -> %p(%s) tr=%d rc=%d\n", (void*)ref, referent, 
          getClassName(referent), referent->isTrackable(), referent->getRootRefCount());
      //rtHeapEx::print_ghost_anchors(referent);
    }
  }
  rtgc_log(true, "final q %p\n", g_finalList._ref_q);
  complete_gc->do_void();
  RefListBase::flush_penging_list();
}


void rtHeap::process_weak_soft_references(OopClosure* keep_alive, VoidClosure* complete_gc, ReferencePolicy* policy) {
  // const char* ref_type$ = reference_type_to_string(clear_ref);
  // __process_java_references<REF_NONE, true>(keep_alive, complete_gc);
  rtHeapEx::_soft_ref_timestamp_clock = java_lang_ref_SoftReference::clock();
  rtgc_log(false, "_soft_ref_timestamp_clock * %lu\n", rtHeapEx::_soft_ref_timestamp_clock);

  if (policy != NULL) {
    g_cntCleanRef ++;
    GCObject* ref;
    for (RefIterator iter(g_finalList); (ref = iter.next_ref<false, false>()) != NULL; ) {
      to_obj(iter.referent_p())->unmarkActiveFinalizereReachable();
    }

  rtgc_log(true, "g_softList 1-1 %d\n", g_softList._refs.size());
    for (RefIterator iter(g_softList); (ref = iter.next_ref<false, true>()) != NULL; ) {
      if (policy->should_clear_reference(iter.ref(), rtHeapEx::_soft_ref_timestamp_clock)) {
        ref->markDirtyReferrerPoints();
        iter.break_weak_soft_link();
      }
    }
    
    for (RefIterator iter(g_weakList); (ref = iter.next_ref<false, true>()) != NULL; ) {
      ref->markDirtyReferrerPoints();
      iter.break_weak_soft_link();
    }

    if (!rtHeap::DoCrossCheck) {
      rtHeap::iterate_young_roots(NULL, true);
    }
    
  rtgc_log(true, "g_softList 1-2 %d\n", g_softList._refs.size());
    for (RefIterator iter(g_softList); (ref = iter.next_ref<true, false>()) != NULL; ) {
      if (ref->isDirtyReferrerPoints()) {
        iter.clear_weak_soft_garbage_referent();
        ref->unmarkDirtyReferrerPoints();
      }
    }

    for (RefIterator iter(g_weakList); (ref = iter.next_ref<true, false>()) != NULL; ) {
      precond(ref->isDirtyReferrerPoints());
      iter.clear_weak_soft_garbage_referent();
      ref->unmarkDirtyReferrerPoints();
    }
    // g_softList.break_reference_links(policy);
    // g_weakList.break_reference_links(policy);
  }
  if (!rtHeap::DoCrossCheck) {
    rtHeapEx::update_soft_ref_master_clock();
  }
}


void rtHeapEx::keep_alive_final_referents(RefProcProxyTask* proxy_task) {
  SerialGCRefProcProxyTask* task = (SerialGCRefProcProxyTask*)proxy_task;
  OopClosure* keep_alive = task->keep_alive_closure();
  VoidClosure* complete_gc = task->complete_gc_closure();

  if (rtHeap::in_full_gc) {
    if (UseCompressedOops) {
      __keep_alive_final_referents<narrowOop, true>(keep_alive, complete_gc);
    } else {
      __keep_alive_final_referents<oop, true>(keep_alive, complete_gc);
    }
  } else {
    if (UseCompressedOops) {
      __keep_alive_final_referents<narrowOop, false>(keep_alive, complete_gc);
    } else {
      __keep_alive_final_referents<oop, false>(keep_alive, complete_gc);
    }
  }  
}

template <bool is_full_gc>
void __process_final_phantom_references() {
  // rtgc_log(true, "g_finalList %d\n", g_finalList._refs.size());
  // for (RefIterator iter(g_finalList); iter.next_ref<false, false>() != NULL; ) {
  //   if (!rtHeap::is_alive(iter.referent_p)) {
  //     iter.enqueue_curr_ref();
  //   } else {
  //     iter.adjust_ref_pointer<is_full_gc>();
  //   }
  // } 
  // phantom referent 에 대한 forwading 처리 필요!!

  // rtgc_log(true, "g_phantomList %p\n", g_phantomList._ref_q);
  for (RefIterator iter(g_phantomList); iter.next_ref<true, true>() != NULL; ) {
    precond((void*)iter.referent_p() != NULL);
    precond((void*)iter.referent_p() > (void*)0x30);
    bool is_alive = rtHeap::is_alive(iter.referent_p());
    if (!is_full_gc) {
      iter.adjust_ref_pointer<false>();
    }
    if (!is_alive) {
      iter.enqueue_curr_ref();
    } else if (!is_full_gc) {
      iter.adjust_referent_pointer<false>();
      // rtgc_log(true, "A) phantom ref %p -> %p\n", iter.ref(), iter.referent_p())
    }
  } 

  RefListBase::flush_penging_list();
}

void rtHeap::process_final_phantom_references(bool is_tenure_gc) {
  if (is_tenure_gc) {
    __process_final_phantom_references<true>();
  } else {
    __process_final_phantom_references<false>();
  }
}

void  __check_garbage_referents() {
  for (int i = 0; i < g_garbage_referents.size(); ) {
    oopDesc* ref_p = g_garbage_referents.at(i++);
    oopDesc* referent_p = g_garbage_referents.at(i++);

    ReferenceType refType = InstanceKlass::cast(ref_p->klass())->reference_type();
    precond(refType != REF_PHANTOM);

    GCObject* referent_node = to_obj(referent_p);
    g_cntGarbageRef ++;
    if (java_lang_ref_Reference::is_final(ref_p)) {
      rtgc_log(true, "final ref cleared * %p -> %p\n", (void*)ref_p, referent_node);
      precond(!referent_node->isGarbageMarked());
      assert(referent_node->getRootRefCount() == 0, "final refrent %p rc=%d\n",
          referent_node, referent_node->getRootRefCount());
    }
    else if (rtHeap::is_alive(referent_p)) {
      g_cntMisRef ++;
      rtgc_log(true, "++g_cntMisRef %s %p -> %p tr=%d, rc=%d, %d/%d\n", 
          reference_type_to_string(refType), ref_p, referent_node, referent_node->isTrackable(), referent_node->getRootRefCount(),
          g_cntMisRef, g_cntGarbageRef);
    }
  }
  g_garbage_referents.resize(0);
  assert(g_cntMisRef == 0, "g_cntMisRef %d/%d\n", g_cntMisRef, g_cntGarbageRef);
  g_cntMisRef = g_cntGarbageRef = 0;
}


bool rtHeap::is_active_finalizer_reachable(oopDesc* final_referent) {
  return to_obj(final_referent)->isActiveFinalizerReachable();
}

template<bool is_full_gc>
void __adjust_ref_q_pointers() {
#if DO_CROSS_CHECK_REF
  __check_garbage_referents();
#endif  
  rtgc_log(true, "g_softList 2 %d\n", g_softList._refs.size());
  for (RefIterator iter(g_softList); iter.next_ref<true, !is_full_gc>() != NULL; ) {
    iter.adjust_ref_pointer<is_full_gc>();
    iter.adjust_referent_pointer<is_full_gc>();
  } 
  rtgc_log(true, "g_weakList 2 %d\n", g_weakList._refs.size());
  for (RefIterator iter(g_weakList); iter.next_ref<true, !is_full_gc>() != NULL; ) {
    iter.adjust_ref_pointer<is_full_gc>();
    iter.adjust_referent_pointer<is_full_gc>();
  } 

  if (is_full_gc) {
    rtgc_log(true, "g_finalList 2 %p\n", g_finalList._ref_q);
    for (RefIterator iter(g_finalList); iter.next_ref<false, false>() != NULL; ) {
      iter.adjust_ref_pointer<is_full_gc>();
      iter.adjust_referent_pointer<is_full_gc>();
    } 

    rtgc_log(true, "g_phantomList 2 %p\n", g_phantomList._ref_q);
    for (RefIterator iter(g_phantomList); iter.next_ref<false, false>() != NULL; ) {
      iter.adjust_ref_pointer<is_full_gc>();
      iter.adjust_referent_pointer<is_full_gc>();
    } 
  }
}

void rtHeapEx::adjust_ref_q_pointers(bool is_full_gc) {
  // #if DO_CROSS_CHECK_REF
  //   __adjust_points(&g_softList._refs, is_full_gc, false, REF_SOFT);
  //   __adjust_points(&g_weakList._refs, is_full_gc, false, REF_WEAK);
  //   // __adjust_points(&g_finalList._refs, is_full_gc, false, REF_FINAL);
  // #endif
  if (is_full_gc) {
    __adjust_ref_q_pointers<true>();
  } else {
    __adjust_ref_q_pointers<false>();
  }
}

void rtHeap__ensure_garbage_referent(oopDesc* ref_p, oopDesc* referent_p, bool clear_soft) {
  precond(rtHeap::DoCrossCheck);
  g_garbage_referents.push_back(ref_p);
  g_garbage_referents.push_back(referent_p);
}

static void __validate_trackable_refs(HugeArray<oop>* _refs) {
  int cntTrackable = 0;
  for (int i = _refs->size(); --i >= 0; ) {
    GCObject* node = to_obj(_refs->at(i));
    if (node->isTrackable()) cntTrackable ++;
  }
  assert(cntTrackable == _refs->size(), " cntTrackable %d/%d\n", cntTrackable, _refs->size());
}

void rtHeapEx::validate_trackable_refs() {
  __validate_trackable_refs(&g_softList._refs);
  __validate_trackable_refs(&g_weakList._refs);
}

void rtHeap::init_java_reference(oopDesc* ref, oopDesc* referent_p) {
  precond(RtNoDiscoverPhantom);
  precond(referent_p != NULL);

  ptrdiff_t referent_offset = java_lang_ref_Reference::referent_offset();
  ReferenceType refType = InstanceKlass::cast(ref->klass())->reference_type();
  oopDesc** ref_q;
  switch (refType) {
    case REF_PHANTOM:
      ref_q = &g_phantomList._ref_q;
      rtgc_log(LOG_OPT(3), "created Phantom ref %p for %p\n", (void*)ref, referent_p);
      break;

    case REF_FINAL:
      ref_q = &g_finalList._ref_q;
      to_obj(referent_p)->markActiveFinalizereReachable();
      rtgc_log(LOG_OPT(3), "created Final ref %p for %p\n", (void*)ref, referent_p);
      break;

    default:
      // TODO -> age 변경 시점을 늦춘다(?).
      ref->set_mark(ref->mark().set_age(markWord::max_age));
      ref_q = refType == REF_WEAK ? &g_weakList._ref_q : &g_softList._ref_q;
      rtgc_log(false, "weak=%d/soft ref %p -> %p age = %d\n", 
          refType == REF_WEAK, (void*)ref, referent_p, ref->age());
      #if DO_CROSS_CHECK_REF
        HeapAccess<>::oop_store_at(ref, referent_offset, referent_p);
        if (refType == REF_WEAK) {
          g_weakList._refs->push_back(ref);
        } else {
          g_softList._refs->push_back(ref);
        }
        return;
      #endif
      break;
  }

  precond(!to_obj(ref)->isTrackable());
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, referent_offset, referent_p);

  oop next_discovered = Atomic::xchg(ref_q, ref);
  java_lang_ref_Reference::set_discovered_raw(ref, next_discovered);
  return;
}

