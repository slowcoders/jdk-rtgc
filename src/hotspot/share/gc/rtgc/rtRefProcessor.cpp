#include "gc/rtgc/rtRefProcessor.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "gc/serial/serialGcRefProcProxyTask.hpp"
#include "gc/shared/genCollectedHeap.hpp"

using namespace rtHeapUtil;
using namespace RTGC;

#define DO_CROSS_CHECK_REF 1
#define PARTIAL_COLLECTION false

int rtHeap::DoCrossCheck = DO_CROSS_CHECK_REF;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_REF, function);
}

extern void rtHeap__addRootStack_unsafe(GCObject* node);

#if DO_CROSS_CHECK_REF
static int g_cntMisRef = 0;
static int g_cntGarbageRef = 0;
static int g_cntCleanRef = 0;
static HugeArray<oop> g_enqued_referents;
static const bool USE_REF_ARRAY = true;
#endif

namespace RTGC {

  static ReferenceType __getRefType(oop ref_p) {
    return InstanceKlass::cast(ref_p->klass())->reference_type();
  }

  class RefList {
  public:
    oopDesc* _ref_q;
    ReferenceType _ref_type;
  #if DO_CROSS_CHECK_REF
    HugeArray<oop> _refs;
    int _refs_lock;
    bool _enable_cross_check;
  #endif
    static int _referent_off;
    static int _discovered_off;
    static oop g_pending_head;
    static oop g_pending_tail;

    RefList(ReferenceType ref_type, bool enable_cross_check) { 
      _ref_q = NULL; 
      _ref_type = ref_type;
      _refs_lock = 0;
      _enable_cross_check = enable_cross_check;
    }

    ReferenceType ref_type() {
      return _ref_type;
    }

    void register_ref(oopDesc* ref, oopDesc* referent_p) {
      rtgc_log(LOG_OPT(3), 
          "created ref-%d %p for %p\n", _ref_type, (void*)ref, (void*)referent_p);
      if (USE_REF_ARRAY) {
        while (Atomic::cmpxchg(&_refs_lock, 0, 1) != 0) { /* do spin. */ }
        _refs->push_back(ref);
        Atomic::release_store(&_refs_lock, 0);
      }
      else {
        while (true) {
          oopDesc* head = _ref_q;
          HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, RefList::_discovered_off, head);
          if (head == Atomic::cmpxchg(&_ref_q, head, ref)) break;
        }
      }
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, _referent_off, referent_p);
      if (!_enable_cross_check) {
        *(int32_t*)((address)ref + _discovered_off) = -1;
      }
    }

    static bool link_pending_reference(oop anchor, oop link) {
      // rtgc_log(LOG_OPT(3), "link_pending_reference %p -> %p\n", (void*)anchor, (void*)link);
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(anchor, RefList::_discovered_off, link);
      precond(link == RawAccess<>::oop_load_at(anchor, RefList::_discovered_off));
      if (link == NULL) return false;

      if (to_node(anchor)->isTrackable()) {
        precond(!to_obj(link)->isGarbageMarked());
        RTGC::add_referrer_ex(link, anchor, !rtHeap::in_full_gc || PARTIAL_COLLECTION);
      }
      return true;
    }

    static void enqueue_pending_ref(oop ref_p) {
      if (!link_pending_reference(ref_p, g_pending_head)) {
        g_pending_tail = ref_p;
      }
      g_pending_head = ref_p;
    }

    static void flush_penging_list() {
      if (g_pending_head == NULL) return;

      oop enqueued_top_np;
      while (true) {
        enqueued_top_np = Universe::reference_pending_list();
        if (enqueued_top_np != NULL && rtHeap::is_trackable(enqueued_top_np)) {
          // 1) swap_reference_pending_list 에 의해 _unsafeList 에 등록되지 않도록 하기 위하여;
          //    g_pending_tail 의 trackable 여부는 무시한다.
          // 2) GC 종료 후 clearStackList 실행 시에,
          //    g_pending_tail 이 UnsafeTrackable 인 경우 _unsafeList 에 등록된다.
          GCRuntime::onAssignRootVariable_internal(to_obj(enqueued_top_np));
          rtHeap__addRootStack_unsafe(to_obj(enqueued_top_np));
        }
        if (enqueued_top_np == Universe::swap_reference_pending_list(g_pending_head)) break;
      }
      link_pending_reference(g_pending_tail, enqueued_top_np);
  #ifdef ASSERT
      for (oop ref_p = g_pending_head; ref_p != enqueued_top_np; ref_p = RawAccess<>::oop_load_at(ref_p, RefList::_discovered_off)) {
        oop r = RawAccess<>::oop_load_at(ref_p, RefList::_referent_off);
        // rtgc_log(LOG_OPT(3), "ref %p(%s) %p", (void*)ref_p, getClassName(to_obj(ref_p)), (void*)r);
        assert(r == NULL || InstanceKlass::cast(ref_p->klass())->reference_type() == REF_FINAL, "ref %p(%s)", (void*)ref_p, getClassName(to_obj(ref_p)));
      }
  #endif
      g_pending_head = NULL;
    }
  };

  enum SkipPolicy {
    SkipNone = 0,
    SkipGarbageRef = 1,
    SkipClearedRef = 2,
    SkipInvalidRef = SkipGarbageRef | SkipClearedRef,
    NoGarbageCheck = 4,
    NoReferentCheck = 8,
    DetectGarbageRef = 16,
    SkipClearedRef_NoGarbageCheck = SkipClearedRef | NoGarbageCheck,
    SkipGarbageRef_NoReferentCheck = SkipGarbageRef | NoReferentCheck,
  };

  template <bool is_full_gc>
  class RefIterator  {
    RefList& _refList;
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
    RefIterator(RefList &refProcessor) : _refList(refProcessor) {
      _next_ref = refProcessor._ref_q;
      do_cross_test = refProcessor._enable_cross_check;

      _curr_ref = _prev_ref_op = NULL;
      _discovered_off = refProcessor._discovered_off;
      _referent_off = refProcessor._referent_off;
      _referent_p = NULL;
      _ref_removed = false;
      _idx = refProcessor._refs.size();
    }

    bool in_cross_test_mode() {
      return do_cross_test;
    }

    oopDesc* ref() {
      return _curr_ref;
    }

    oopDesc* referent() {
      return _referent_p;
    }

    HeapWord* referent_addr() {
      return java_lang_ref_Reference::referent_addr_raw(_curr_ref);
    }

    oopDesc* get_raw_referent() {
      oop p = RawAccess<>::oop_load_at(_curr_ref, _referent_off);
      return p;
    }

    bool is_garbage_ref(SkipPolicy policy) {
      GCObject* node = to_obj(_curr_ref);
      if (!node->isTrackable()) {
          return !_curr_ref->is_gc_marked(); 
      }
      if (!(policy & DetectGarbageRef)) {
        return node->isGarbageMarked();
      } 
      
      bool is_garbage = _rtgc.g_pGarbageProcessor->detectGarbage(node, is_full_gc);
      if (is_garbage) {
        // precond(node->isDirtyReferrerPoints());
        node->unmarkDirtyReferrerPoints();
      }
      return is_garbage;
    }

    GCObject* next_ref(SkipPolicy policy) {
      while (true) {
        if (USE_REF_ARRAY) {
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
          postcond(_curr_ref != _next_ref);
        }

        if (!(policy & (SkipGarbageRef | DetectGarbageRef))) {
          precond((policy & NoGarbageCheck) || rtHeap::is_alive(_curr_ref));
        } else if (is_garbage_ref(policy)) {
          rtgc_log(false && _refList.ref_type() == REF_SOFT, 
              "garbage soft ref %p\n", (void*)_curr_ref);
          assert(!do_cross_test || !_curr_ref->is_gc_marked() || rtHeapUtil::is_dead_space(_curr_ref), 
              "invalid gargabe %p(%s) policy=%d old_gen_start=%p tr=%d, rc=%d hasReferrer=%d ghost=%d\n", 
              (void*)_curr_ref, RTGC::getClassName(to_obj(_curr_ref)), policy, 
              GenCollectedHeap::heap()->old_gen()->reserved().start(),
              to_obj(_curr_ref)->isTrackable(), to_obj(_curr_ref)->getRootRefCount(), to_obj(_curr_ref)->hasReferrer(), 
              rtHeapEx::print_ghost_anchors(to_obj(_curr_ref)));
          this->remove_curr_ref(true);
          continue;
        }
        precond(__getRefType(_curr_ref) == _refList.ref_type());

        if ((policy & NoReferentCheck)) {
          _referent_p = 0;
          break;
        }

        _referent_p = get_raw_referent();

        if (!(policy & SkipClearedRef)) {
          postcond(_referent_p != (USE_REF_ARRAY || do_cross_test ? oop(NULL) : _curr_ref));
        }
        else if (_referent_p == (USE_REF_ARRAY || do_cross_test ? oop(NULL) : _curr_ref)) {
          rtgc_log(LOG_OPT(3), "remove cleared ref %p\n", (void*)_curr_ref);
          if (!is_full_gc) {
            adjust_ref_pointer();
          }
          clear_referent();
          this->remove_curr_ref(true);
          continue;
        }
        break;
      }
      return to_obj(_curr_ref);
    }

    oop get_valid_forwardee(oop old_p) {
      if (is_full_gc) {
        oop new_p = old_p->forwardee();
        return new_p == NULL ? old_p : new_p;
      } else if (to_obj(old_p)->isTrackable()) {
        return old_p;
      } else {
        return old_p->forwardee();
      }
    }

    bool adjust_ref_pointer() {
      precond(!rtHeap::DoCrossCheck || _curr_ref->is_gc_marked() || (!rtHeap::in_full_gc && to_obj(_curr_ref)->isTrackable()));
      oop new_p = get_valid_forwardee(_curr_ref);
      if (new_p != _curr_ref) {
        if (USE_REF_ARRAY) {
          _refList._refs.at(_idx) = new_p;
        }
        else if (_prev_ref_op == NULL) {
          _refList._ref_q = new_p;
        } else {
          RawAccess<>::oop_store_at(_prev_ref_op, _discovered_off, new_p);
        }

        if (!is_full_gc) {
          _curr_ref = new_p;
        } else {
          // 객체 복사가 되지 않은 상태.
        }
      } else if (!is_full_gc) {
        precond(!RTGC::debugOptions[0] || to_obj(_curr_ref)->isTrackable());
        return to_obj(_curr_ref)->isTrackable();
      } else {
        postcond(is_full_gc || to_obj(_curr_ref)->isTrackable());
      }
      return true;
    }

    void adjust_referent_pointer() {
      if (do_cross_test) return;
      
      oop new_p = get_valid_forwardee(_referent_p);
      if (new_p == _referent_p) {
        postcond(is_full_gc || to_obj(_referent_p)->isTrackable());
      } else {
        // rtgc_log(LOG_OPT(3), "move referent %p -> %p\n", (void*)old_p, (void*)new_p);
        RawAccess<>::oop_store_at(_curr_ref, _referent_off, new_p);
        if (!is_full_gc) {
          _referent_p = new_p;
        }
      }
    }

    void enqueue_curr_ref(bool clear_referent_p) {
      if (clear_referent_p) {
        clear_referent();
      }
      oop ref_p = _curr_ref;
      remove_curr_ref(false);
      
      if (do_cross_test) {
        g_enqued_referents.push_back(ref_p);
        return;
      }
      RefList::enqueue_pending_ref(ref_p);
    }

    void remove_curr_ref(bool do_clear_discovered) {
      if (do_clear_discovered) {
        clear_discovered();
      }
      if (USE_REF_ARRAY) {
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
      rtgc_log(LOG_OPT(3),
          "referent<%d> %p(%s) alive=%d, tr=%d gm=%d refT=%d multi=%d\n", 
          _refList.ref_type(), referent, RTGC::getClassName(referent), rtHeap::is_alive(_referent_p),
          referent->isTrackable(), referent->isGarbageMarked(), _refList.ref_type(), referent->hasMultiRef());
      if (!rtHeap::is_alive(_referent_p)) {
        assert(!_referent_p->is_gc_marked(),
            "referent %p(%s) tr=%d gm=%d refT=%d multi=%d\n", referent, RTGC::getClassName(referent), 
            referent->isTrackable(), referent->isGarbageMarked(), _refList.ref_type(), referent->hasMultiRef());
        referent->removeBrokenAnchors();
        enqueue_curr_ref(true);
      }
    }

  private:  
    void clear_discovered() {
      if (do_cross_test) return;
      RawAccess<>::oop_store_at(_curr_ref, _discovered_off, oop(NULL));
    }

    void clear_referent() {
      if (do_cross_test/* && !to_obj(_referent_p)->isTrackable()*/) return;
      // rtgc_log(LOG_OPT(3), "clear referent ref %p\n", (void*)_curr_ref);
      RawAccess<>::oop_store_at(_curr_ref, _referent_off, oop(NULL));
    }

  };
};

using namespace RTGC;
oop RefList::g_pending_head = NULL;
oop RefList::g_pending_tail;
int RefList::_referent_off;
int RefList::_discovered_off;


static RefList g_softList(REF_SOFT, true);
static RefList g_weakList(REF_WEAK, true);
static RefList g_finalList(REF_FINAL, false);
static RefList g_phantomList(REF_PHANTOM, false);

void rtHeapEx::initializeRefProcessor() {
  RefList::_referent_off = java_lang_ref_Reference::referent_offset();
  RefList::_discovered_off = java_lang_ref_Reference::discovered_offset();
  postcond(RefList::_referent_off != 0);
  postcond(RefList::_discovered_off != 0);
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

jlong __get_soft_ref_timestamp_clock(bool reset) {
  if (reset) {
    rtHeapEx::_soft_ref_timestamp_clock = java_lang_ref_SoftReference::clock();    
    // rtgc_log(LOG_OPT(3), "reset _soft_ref_timestamp_clock * %lu\n", rtHeapEx::_soft_ref_timestamp_clock);
  }
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



void rtHeap::link_discovered_pending_reference(oopDesc* ref_q, oopDesc* end) {
  rtgc_log(LOG_OPT(3), "link_discovered_pending_reference from %p to %p\n", (void*)ref_q, end);
  oopDesc* discovered;
  for (oopDesc* obj = ref_q; obj != end; obj = discovered) {
    discovered = java_lang_ref_Reference::discovered(obj);
    if (to_obj(obj)->isTrackable()) {
      RTGC::add_referrer_ex(discovered, obj, !rtHeap::in_full_gc || PARTIAL_COLLECTION);
    }
  }
}



void rtHeap__clear_garbage_young_roots(bool is_full_gc);

template<typename T, bool is_full_gc>
static void __keep_alive_final_referents(OopClosure* keep_alive, VoidClosure* complete_gc) {
  GCObject* ref;
  for (RefIterator<is_full_gc> iter(g_finalList); (ref = iter.next_ref(SkipNone)) != NULL; ) {
    GCObject* referent = to_obj(iter.referent());
    bool is_new_ref = true;
    if (is_full_gc) {
      if (referent->isTrackable()) {
        _rtgc.g_pGarbageProcessor->detectGarbage(referent, false);
      }
    } else {
      is_new_ref = iter.adjust_ref_pointer();
      precond(is_new_ref);
      ref = to_obj(iter.ref());
      postcond((void*)referent == iter.get_raw_referent());
    }
    bool is_gc_marked;
    if (rtHeap::DoCrossCheck && is_full_gc) { 
      is_gc_marked = cast_to_oop(referent)->is_gc_marked();
      assert(!referent->isTrackable() || is_gc_marked == (!referent->isGarbageMarked() && referent->isStrongReachable()), 
          "damaged referent %p(%s) gc_mark=%d rc=%d, unsafe=%d hasReferer=%d garbage=%d ghost=%d\n", 
          referent, RTGC::getClassName(referent), is_gc_marked, referent->getRootRefCount(), 
          referent->isUnstableMarked(), 
          referent->hasReferrer(), referent->isGarbageMarked(), rtHeapEx::print_ghost_anchors(referent));
    } else {
      is_gc_marked = referent->isTrackable() ? referent->isStrongReachable() : cast_to_oop(referent)->is_gc_marked();
    }
    if (!is_gc_marked) {
      if (true) {
        rtgc_log(LOG_OPT(3), "resurrect final ref %p of %p\n", ref, referent);
        if (is_full_gc && rtHeap::DoCrossCheck) {
          MarkSweep::_is_rt_anchor_trackable = ref->isTrackable();
        }
        referent->unmarkGarbage();
        referent->unmarkDirtyReferrerPoints();
        keep_alive->do_oop((T*)iter.referent_addr());
      }
      GCObject* old_referent = referent;
      if (is_full_gc) {
        postcond(referent == (void*)iter.get_raw_referent());
      } else {
        referent = to_obj(iter.get_raw_referent());
        referent->unmarkActiveFinalizerReachable();
      }
      /* referent 가 순환 가비지의 일부이면, referrerList.size()가 0보다 크다 */
      postcond(!referent->isGarbageMarked());
      postcond(!referent->hasSafeAnchor());
      postcond(!referent->hasShortcut());
      postcond(referent->getRootRefCount() == 0);
      postcond(!rtHeap::DoCrossCheck || cast_to_oop(old_referent)->is_gc_marked() ||
          (!is_full_gc && old_referent->isTrackable()));
      postcond(!referent->isActiveFinalizerReachable());
      rtgc_log(LOG_OPT(3), "final ref cleared 1 %p -> %p(%p)(%s)\n", 
          (void*)ref, old_referent, referent, RTGC::getClassName(old_referent));
      if (ref->isTrackable()) {
        RTGC::add_referrer_ex(cast_to_oop(referent), cast_to_oop(ref), !is_full_gc || PARTIAL_COLLECTION);
        if (referent->isTrackable()) {
          referent->setSafeAnchor(ref);
          referent->setShortcutId_unsafe(INVALID_SHORTCUT);
        }
      } else if (referent->isTrackable()) {
        // gc 종료 후 Unsafe List 등록되도록 한다.
        // rtgc_log(LOG_OPT(3), "yg-reachable final referent %p\n", referent);
        rtHeap::mark_survivor_reachable(cast_to_oop(referent));
      }
      iter.enqueue_curr_ref(false);
    } else {
      rtgc_log(LOG_OPT(3), "active final ref %p of %p(%s)\n", ref, referent, getClassName(referent));
      if (is_full_gc) {
        // remark!
        referent->markActiveFinalizerReachable();
      } else if (is_new_ref) {
        iter.adjust_referent_pointer();
      }
      assert(referent->isActiveFinalizerReachable(), "must be ActiveFinalizerReachable %p\n", referent);
    }
  }
  rtgc_log(LOG_OPT(3), "final q %p\n", g_finalList._ref_q);
  complete_gc->do_void();
  RefList::flush_penging_list();
  rtHeap__clear_garbage_young_roots(is_full_gc);
  if (!is_full_gc) {
    rtHeapEx::adjust_ref_q_pointers(false);
  }
#if DO_CROSS_CHECK_REF  
  rtHeap::DoCrossCheck = 1;
#endif

}

void rtHeapEx::clear_finalizer_reachables() {
  GCObject* ref;
  for (RefIterator<true> iter(g_finalList); (ref = iter.next_ref(SkipNone)) != NULL; ) {
    to_obj(iter.referent())->unmarkActiveFinalizerReachable();
  }
}

void rtHeap::process_weak_soft_references(OopClosure* keep_alive, VoidClosure* complete_gc, ReferencePolicy* policy) {
  // const char* ref_type$ = reference_type_to_string(clear_ref);
  // __process_java_references<REF_NONE, true>(keep_alive, complete_gc);
  rtHeapEx::_soft_ref_timestamp_clock = java_lang_ref_SoftReference::clock();
  // rtgc_log(LOG_OPT(3), "_soft_ref_timestamp_clock * %lu\n", rtHeapEx::_soft_ref_timestamp_clock);

  if (policy != NULL) {
    precond(rtHeap::in_full_gc);
    
    rtHeapEx::clear_finalizer_reachables();

    GCObject* ref;

    rtgc_log(LOG_OPT(3), "g_softList 1-1 %d\n", g_softList._refs.size());
    for (RefIterator<true> iter(g_softList); (ref = iter.next_ref(SkipClearedRef_NoGarbageCheck)) != NULL; ) {
      if (policy->should_clear_reference(iter.ref(), rtHeapEx::_soft_ref_timestamp_clock)) {
        ref->markDirtyReferrerPoints();
        rtgc_log(LOG_OPT(3), "dirty soft %p tr=%d\n", ref, ref->isTrackable());
        if (ref->isTrackable()) {
          iter.break_weak_soft_link();
        }
      }
      // else if (rtHeap::DoCrossCheck || !to_obj(iter.referent())->isTrackable()) {
      //   if (UseCompressedOops) {
      //     keep_alive->do_oop((narrowOop*)iter.referent_addr());
      //   } else {
      //     keep_alive->do_oop((oop*)iter.referent_addr());
      //   }
      // }
    }
    
    for (RefIterator<true> iter(g_weakList); (ref = iter.next_ref(SkipClearedRef_NoGarbageCheck)) != NULL; ) {
      ref->markDirtyReferrerPoints();
      if (ref->isTrackable()) {
        iter.break_weak_soft_link();
      }
    }

#if DO_CROSS_CHECK_REF  
    rtHeap::DoCrossCheck = -1;
#endif
    rtHeap::iterate_younger_gen_roots(NULL, true);
    complete_gc->do_void();

    rtgc_log(LOG_OPT(3), "g_softList 1-2 %d\n", g_softList._refs.size());
    for (RefIterator<true> iter(g_softList); (ref = iter.next_ref(DetectGarbageRef)) != NULL; ) {
      if (ref->isDirtyReferrerPoints()) {
        rtgc_log(LOG_OPT(3), "clear dirty soft %p tr=%d\n", ref, ref->isTrackable());
        iter.clear_weak_soft_garbage_referent();
      }
    }

    for (RefIterator<true> iter(g_weakList); (ref = iter.next_ref(DetectGarbageRef)) != NULL; ) {
      precond(ref->isDirtyReferrerPoints());
      iter.clear_weak_soft_garbage_referent();
    }

    if (true) {
      for (RefIterator<true> iter(g_softList); (ref = iter.next_ref(SkipNone)) != NULL; ) {
        if (ref->isDirtyReferrerPoints()) {
          ref->unmarkDirtyReferrerPoints();
        }
      }

      for (RefIterator<true> iter(g_weakList); (ref = iter.next_ref(SkipNone)) != NULL; ) {
        precond(ref->isDirtyReferrerPoints());
        ref->unmarkDirtyReferrerPoints();
      }
    }
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
  // rtgc_log(LOG_OPT(3), "g_phantomList %p\n", g_phantomList._ref_q);
  for (RefIterator<is_full_gc> iter(g_phantomList); iter.next_ref(SkipInvalidRef) != NULL; ) {
    oopDesc* old_referent = iter.referent();
    precond(old_referent != NULL);
    bool is_alive = rtHeap::is_alive(old_referent);
    bool is_new_ref = !is_full_gc && iter.adjust_ref_pointer();
    precond(is_full_gc || is_new_ref);
    if (!is_alive) {
      precond(!to_obj(old_referent)->isTrackable() || to_obj(old_referent)->isDestroyed());
      iter.enqueue_curr_ref(true);
    } else if (is_new_ref) {
      iter.adjust_referent_pointer();
    }
    rtgc_log(LOG_OPT(5), 
      "check phantom ref) %p(alive=%d) of %p -> %p\n", iter.ref(), is_alive, old_referent, iter.referent());
  } 

  RefList::flush_penging_list();
}

extern bool g_lock_unsafe_buff;
void rtHeap::process_final_phantom_references(bool is_tenure_gc) {
  if (is_tenure_gc) {
    precond(!_rtgc.g_pGarbageProcessor->hasUnsafeObjects());
    g_lock_unsafe_buff = true;
    __process_final_phantom_references<true>();
    precond(!_rtgc.g_pGarbageProcessor->hasUnsafeObjects());
  } else {
    __process_final_phantom_references<false>();
  }
}

// void  __check_garbage_referents() {
//   for (int i = 0; i < g_enqued_referents.size(); ) {
//     oopDesc* ref_p = g_enqued_referents.at(i++);
//     oopDesc* referent_p = g_enqued_referents.at(i++);

//     ReferenceType refType = InstanceKlass::cast(ref_p->klass())->reference_type();
//     precond(refType != REF_PHANTOM);

//     GCObject* referent_node = to_obj(referent_p);
//     g_cntGarbageRef ++;
//     if (java_lang_ref_Reference::is_final(ref_p)) {
//       rtgc_log(LOG_OPT(3), "final referent cleared * %p -> %p\n", (void*)ref_p, referent_node);
//       precond(!referent_node->isGarbageMarked());
//       assert(referent_node->getRootRefCount() == 0, "final refrent %p rc=%d\n",
//           referent_node, referent_node->getRootRefCount());
//     }
//     else if (rtHeap::is_alive(referent_p)) {
//       g_cntMisRef ++;
//       rtgc_log(1, "++g_cntMisRef %s %p -> %p(%s) tr=%d, rc=%d, hasReferer=%d isClass=%d\n", 
//           reference_type_to_string(refType), ref_p, referent_node, 
//           RTGC::getClassName(referent_node),
//           referent_node->isTrackable(), referent_node->getRootRefCount(),
//           referent_node->hasReferrer(), cast_to_oop(referent_node)->klass() == vmClasses::Class_klass());
//     }
//   }
//   g_enqued_referents.resize(0);
//   assert(g_cntMisRef == 0, "g_cntMisRef %d/%d\n", g_cntMisRef, g_cntGarbageRef);
//   g_cntMisRef = g_cntGarbageRef = 0;
// }



template<bool is_full_gc>
void __adjust_ref_q_pointers() {
#if DO_CROSS_CHECK_REF
  // __check_garbage_referents();
#endif  
  SkipPolicy soft_weak_policy = is_full_gc ? SkipGarbageRef_NoReferentCheck : SkipInvalidRef;
  rtgc_log(LOG_OPT(3), "g_softList 2 %d\n", g_softList._refs.size());
  for (RefIterator<is_full_gc> iter(g_softList); iter.next_ref(soft_weak_policy) != NULL; ) {
    // bool ref_alive = iter.ref()->is_gc_marked();
    // bool referent_alive = iter.referent()->is_gc_marked();
    iter.adjust_ref_pointer();
    if (!is_full_gc) {
      iter.adjust_referent_pointer();
    }
  } 
  rtgc_log(LOG_OPT(3), "g_weakList 2 %d\n", g_weakList._refs.size());
  for (RefIterator<is_full_gc> iter(g_weakList); iter.next_ref(soft_weak_policy) != NULL; ) {
    iter.adjust_ref_pointer();
    if (!is_full_gc) {
      iter.adjust_referent_pointer();
    }
  } 

  for (RefIterator<is_full_gc> iter(g_finalList); iter.next_ref(NoGarbageCheck) != NULL; ) {
    postcond(to_obj(iter.referent())->isActiveFinalizerReachable());
  } 

  if (is_full_gc) {
    rtgc_log(LOG_OPT(3), "g_finalList 2 %p\n", g_finalList._ref_q);
    for (RefIterator<is_full_gc> iter(g_finalList); iter.next_ref(SkipNone) != NULL; ) {
      iter.adjust_ref_pointer();
      iter.adjust_referent_pointer();
    } 

    rtgc_log(LOG_OPT(3), "g_phantomList 2 %p\n", g_phantomList._ref_q);
    for (RefIterator<is_full_gc> iter(g_phantomList); iter.next_ref(SkipNone) != NULL; ) {
      oopDesc* old_p = iter.referent();
      iter.adjust_ref_pointer();
      iter.adjust_referent_pointer();
      rtgc_log(LOG_OPT(5), 
        "active phantom ref) %p of %p -> %p\n", iter.ref(), old_p, iter.referent());
    } 
  }
}

void rtHeapEx::adjust_ref_q_pointers(bool is_full_gc) {
  if (is_full_gc) {
    __adjust_ref_q_pointers<true>();
  } else {
    __adjust_ref_q_pointers<false>();
  }
}

void rtHeap__ensure_garbage_referent(oopDesc* ref_p, oopDesc* referent_p, bool clear_soft) {
  precond(rtHeap::DoCrossCheck);
  rtgc_log(LOG_OPT(3), "enq ref(%p) of %p\n", ref_p, referent_p);
  int idx = g_enqued_referents.indexOf(ref_p);
  assert(idx >= 0, "ref[%d] %p of %p not found\n", __getRefType(ref_p),  ref_p, referent_p);
  g_enqued_referents.removeFast(idx);
}

static void __validate_trackable_refs(HugeArray<oop>* _refs) {
  int cntTrackable = 0;
  for (int i = _refs->size(); --i >= 0; ) {
    GCObject* node = to_obj(_refs->at(i));
    if (node->isTrackable()) cntTrackable ++;
  }
  rtgc_log(cntTrackable != _refs->size(), "trackable reference %d/%d\n", cntTrackable, _refs->size());
}

void rtHeapEx::validate_trackable_refs() {
  __validate_trackable_refs(&g_softList._refs);
  __validate_trackable_refs(&g_weakList._refs);
}

void rtHeap__assertNoUnsafeObjects() {
  g_lock_unsafe_buff = false;
  precond(!_rtgc.g_pGarbageProcessor->hasUnsafeObjects());
}

bool rtHeap::has_valid_discovered_reference(oopDesc* ref, ReferenceType type) {
  precond(EnableRTGC);
  if (type >= REF_FINAL) {
    bool is_valid = *(int32_t*)((address)ref + RefList::_discovered_off) != -1;
    return is_valid;
  }
  return true;
}

void rtHeap::init_java_reference(oopDesc* ref, oopDesc* referent_p) {
  precond(RtNoDiscoverPhantom);
  precond(referent_p != NULL);

  ReferenceType refType = InstanceKlass::cast(ref->klass())->reference_type();
  switch (refType) {
    case REF_PHANTOM:
      g_phantomList.register_ref(ref, referent_p);
      break;

    case REF_FINAL:
      to_obj(referent_p)->markActiveFinalizerReachable();
      g_finalList.register_ref(ref, referent_p);
      break;

    case REF_WEAK:
      ref->set_mark(ref->mark().set_age(markWord::max_age));
      g_weakList.register_ref(ref, referent_p);
      break;
    
    case REF_SOFT:
      // TODO -> age 변경 시점을 늦춘다(?).
      ref->set_mark(ref->mark().set_age(markWord::max_age));
      g_softList.register_ref(ref, referent_p);
      break;

    default:
      fatal("should not reach here.");
  }

  // 참고) 생성 도중 full-gc 가 발생한 경우, ref 가 trackble 상태일 수 있다.
  return;
}

