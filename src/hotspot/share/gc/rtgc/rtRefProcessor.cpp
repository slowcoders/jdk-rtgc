#include "gc/rtgc/rtHeapEx.hpp"

#include "gc/rtgc/rtgcGlobals.hpp"
#include "impl/GCObject.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "gc/serial/serialGcRefProcProxyTask.hpp"
#include "gc/shared/genCollectedHeap.hpp"
#include "oops/instanceRefKlass.inline.hpp"

using namespace rtHeapUtil;
using namespace RTGC;

#define DO_CROSS_CHECK_REF 1
#define PARTIAL_COLLECTION true

int rtHeap::DoCrossCheck = 0;//DO_CROSS_CHECK_REF;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_REF, function);
}

extern void rtHeap__addRootStack_unsafe(GCObject* node);
extern void rtHeap__clear_garbage_young_roots(bool is_full_gc);

static const bool USE_REF_ARRAY = true;
static const bool ENABLE_SOFT_WEAK_REF = true;
#if DO_CROSS_CHECK_REF
static int g_cntMisRef = 0;
static int g_cntGarbageRef = 0;
static int g_cntCleanRef = 0;
static HugeArray<oop> g_enqued_referents;
#endif

namespace RTGC {

  static ReferenceType __getRefType(oop ref_p) {
    Klass* k = ref_p->klass();
    rt_assert_f(k->is_instance_klass(), PTR_DBG_SIG, PTR_DBG_INFO(ref_p));
    return InstanceKlass::cast(k)->reference_type();
  }

  class RefList {
  public:
    oopDesc* _ref_q;
    ReferenceType _ref_type;
    HugeArray<oop> _refs;
    int _refs_lock;
  #if DO_CROSS_CHECK_REF
    bool _enable_cross_check;
  #endif
    static int _referent_off;
    static int _discovered_off;
    static oop g_pending_head;
    static oop g_pending_tail;
    static ReferencePolicy* g_ref_policy;

    RefList(ReferenceType ref_type, bool enable_cross_check) { 
      _ref_q = NULL; 
      _ref_type = ref_type;
      _refs_lock = 0;
      _enable_cross_check = enable_cross_check;
    }

    void initialize() {
      _refs.initialize();
    }

    ReferenceType ref_type() {
      return _ref_type;
    }

    void register_ref(oopDesc* ref, oopDesc* referent_p) {
      rtgc_log(LOG_OPT(3), 
          "created ref-%d %p for %p", _ref_type, (void*)ref, (void*)referent_p);
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
        //*(int32_t*)((address)ref + _discovered_off) = -1;
      }
    }

    static bool link_pending_reference(oop anchor, oop link) {
      rtgc_log(LOG_OPT(2), "link_pending_reference %p -> %p", (void*)anchor, (void*)link);
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(anchor, RefList::_discovered_off, link);
      rt_assert(link == RawAccess<>::oop_load_at(anchor, RefList::_discovered_off));
      if (link == NULL) return false;

      rt_assert(!to_obj(link)->isGarbageMarked());
      if (to_node(anchor)->isTrackable()) {
        RTGC::add_trackable_link_or_mark_young_root(link, anchor);
      } else if (to_obj(link)->isTrackable()) {
        rt_assert(true && rtHeap::is_alive(link));
        rtHeap::mark_survivor_reachable(link);
      }
      return true;
    }

    static void enqueue_pending_ref(oop ref_p) {
      if (!link_pending_reference(ref_p, g_pending_head)) {
        g_pending_tail = ref_p;
      }
      g_pending_head = ref_p;
    }

    static void hold_object_while_gc(GCObject* obj) {
      if (obj != NULL && obj->isTrackable() && !obj->isSurvivorReachable()) {
        rtHeap__addRootStack_unsafe(obj);
      }
    }

    static void hold_pending_q() {
      // GarbageMarking 되지 않도록 한다. (UnsafeList 등록과 무관)
      hold_object_while_gc(to_obj(g_pending_head));
    }

    static void flush_penging_list(bool is_full_gc) {
      if (g_pending_head == NULL) return;

      oop enqueued_top_np = Universe::reference_pending_list();
      link_pending_reference(g_pending_tail, enqueued_top_np);
      if (is_full_gc) {
        /* garbageCollection() 종료 후, unsafeList 에 객체가 등록되는 것을 방지한다.
          (swap_reference_pending_list() 함수에 의해 enqueued_top_np 가 unsafe 상태가 될 수 있다.)
          아니면, unsafeList 에 대한 adjust_pointers 를 별도로 실행하여야 한다.
        */
        hold_object_while_gc(to_obj(enqueued_top_np));
      }
      oop old = Universe::swap_reference_pending_list(g_pending_head);
      rt_assert(enqueued_top_np == old);
  #ifdef ASSERT
      for (oop ref_p = g_pending_head; ref_p != NULL; ref_p = RawAccess<>::oop_load_at(ref_p, RefList::_discovered_off)) {
        oop r = RawAccess<>::oop_load_at(ref_p, RefList::_referent_off);
        // rtgc_log(LOG_OPT(3), "ref %p(%s) %p", (void*)ref_p, getClassName(to_obj(ref_p)), (void*)r);
        rt_assert_f(r == NULL || InstanceKlass::cast(ref_p->klass())->reference_type() == REF_FINAL, "ref %p(%s)", (void*)ref_p, getClassName(to_obj(ref_p)));
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
    //ClearAnchorList = 32,

    SkipClearedRef_NoGarbageCheck = SkipClearedRef | NoGarbageCheck,
    SkipGarbageRef_NoReferentCheck = SkipGarbageRef | NoReferentCheck,
    // DetectGarbageRef_ClearAnchorList = DetectGarbageRef | ClearAnchorList,
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
      if (node->isGarbageMarked()) {
        return true;
      }

      if (!node->isTrackable_unsafe()) {
        return !_curr_ref->is_gc_marked(); 
      }

      if (policy & DetectGarbageRef) {
        return _rtgc.g_pGarbageProcessor->detectGarbage(node);
      } 
      
      return false;
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
              rt_assert(_refList._ref_q == _curr_ref);
            } else {
              rt_assert(RawAccess<>::oop_load_at(_prev_ref_op, _discovered_off) == _curr_ref);        
            }
          }
          _prev_ref_op = _curr_ref;
          _curr_ref = _next_ref;
          if (_curr_ref == NULL) return NULL;
          _next_ref = RawAccess<>::oop_load_at(_curr_ref, _discovered_off);
          rt_assert(_curr_ref != _next_ref);
        }

        const bool check_garbage = (policy & (SkipGarbageRef | DetectGarbageRef)) != 0;
        if (check_garbage && is_garbage_ref(policy)) {
          if (policy & DetectGarbageRef) {
            GCObject* referent = to_obj(get_raw_referent());
            if (!referent->isGarbageMarked() && referent->clearEmptyAnchorList()) {
              // emptyAnchorList를 명시적으로 clear 한다.
              GCRuntime::detectUnsafeObject(referent);
            }
            if (to_obj(_curr_ref)->isTrackable() && !to_obj(_curr_ref)->getContextFlag()) {
              // referent 의 referrerList 에서 _current_ref 가 제거된 상태.
              // rtgc_log(true, "clear referent field " PTR_DBG_SIG PTR_DBG_SIG, PTR_DBG_INFO(_curr_ref), PTR_DBG_INFO(referent));
              _curr_ref->obj_field_put_raw(_referent_off, NULL);
            }
          }
          rtgc_log(false && _refList.ref_type() == REF_SOFT, 
              "garbage soft ref %p", (void*)_curr_ref);
          rt_assert_f(!_curr_ref->is_gc_marked() || rtHeapUtil::is_dead_space(_curr_ref), 
              "invalid gargabe %p(%s) policy=%d old_gen_start=%p tr=%d, rc=%d ac=%d ghost=%d", 
              (void*)_curr_ref, RTGC::getClassName(to_obj(_curr_ref)), policy, 
              GenCollectedHeap::heap()->old_gen()->reserved().start(),
              to_obj(_curr_ref)->isTrackable(), to_obj(_curr_ref)->getRootRefCount(), to_obj(_curr_ref)->getAnchorCount(), 
              rtHeapEx::print_ghost_anchors(to_obj(_curr_ref)));
          this->remove_curr_ref(false);
          continue;
        }

        rt_assert_f(!to_obj(_curr_ref)->isGarbageMarked(), PTR_DBG_SIG, PTR_DBG_INFO(_curr_ref));
        rt_assert_f(__getRefType(_curr_ref) == _refList.ref_type(), 
              "wrong ref %d expected %d" PTR_DBG_SIG, 
              __getRefType(_curr_ref), _refList.ref_type(), PTR_DBG_INFO(_curr_ref));

        if ((policy & NoReferentCheck)) {
          _referent_p = 0;
          break;
        }

        _referent_p = get_raw_referent();

        if (!(policy & SkipClearedRef)) {
          rt_assert(_referent_p != NULL);
        }
        else if (_referent_p == NULL) {
          rtgc_log(LOG_OPT(3), "remove cleared ref %p", (void*)_curr_ref);
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

    void adjust_ref_pointer() {
      if (rtHeap::DoCrossCheck) {
        rt_assert(_curr_ref->is_gc_marked() || (!rtHeap::in_full_gc && to_obj(_curr_ref)->isTrackable()));
      }
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
        rt_assert(to_obj(_curr_ref)->isTrackable());
      }
      return;
    }

    void adjust_referent_pointer() {
      if (do_cross_test) return;
      
      oop new_p = get_valid_forwardee(_referent_p);
      if (new_p == _referent_p) {
        rt_assert(is_full_gc || to_obj(_referent_p)->isTrackable());
      } else {
        // rtgc_log(LOG_OPT(3), "move referent %p -> %p", (void*)old_p, (void*)new_p);
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
      rtgc_log(LOG_OPT(3), "enqueue_pending_ref %p %d", (void*)ref_p, _refList.ref_type());
      RefList::enqueue_pending_ref(ref_p);
    }

    void remove_curr_ref(bool do_clear_discovered) {
      if (do_clear_discovered) {
        clear_discovered();
      }
      // rtgc_log(_refList.ref_type() == REF_SOFT, "remove ref %d %p", _refList.ref_type(), (void*)_curr_ref);
      if (USE_REF_ARRAY) {
        _refList._refs.removeFast(_idx);
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
      rt_assert(ref->isTrackable());
      if (to_obj(_referent_p)->isTrackable()) {
        to_obj(_referent_p)->removeReferrerWithoutReallocaton(ref);
      }
    } 

    void clear_weak_soft_garbage_referent() {
      GCObject* referent = to_obj(_referent_p);
      bool is_alive;
      if (referent->isTrackable()) {
        is_alive = referent->isStrongRootReachable();
        if (!is_alive) {
          if (referent->isActiveFinalizerReachable()) {
            is_alive = !referent->clearEmptyAnchorList();
          } else {
            is_alive = !_rtgc.g_pGarbageProcessor->detectGarbage(referent);
          }
        }
        if (is_alive) {
          GCObject* ref = to_obj(_curr_ref);
          if (ref->isTrackable()) {
            referent->addTrackableAnchor(ref);
          }
        }
      } else {
        is_alive = _referent_p->is_gc_marked();
        if (!is_alive) referent->clearAnchorList();
      }

      rtgc_log(LOG_OPT(3),
          "ref %p of <%d> %p(%s) alive=%d, tr=%d gm=%d refT=%d multi=%d", 
          (void*)_curr_ref, _refList.ref_type(), referent, RTGC::getClassName(referent), is_alive,
          referent->isTrackable(), referent->isGarbageMarked(), _refList.ref_type(), 
          referent->hasMultiRef());

      if (!is_alive) {
        rt_assert(!_referent_p->is_gc_marked());
        rtgc_log(false && !referent->isTrackable(), //!_referent_p->is_gc_marked(),
            "referent %p(%s) is collected tr=%d gm=%d refT=%d multi=%d", referent, RTGC::getClassName(referent), 
            referent->isTrackable(), referent->isGarbageMarked(), _refList.ref_type(), 
            referent->hasMultiRef());
        enqueue_curr_ref(true);
      } else {
        if ((rtHeap::DoCrossCheck && is_full_gc) || !referent->isTrackable()) {
          rt_assert_f(_referent_p->is_gc_marked(),
              "referent %p(%s) tr=%d gm=%d refT=%d multi=%d", referent, RTGC::getClassName(referent), 
              referent->isTrackable(), referent->isGarbageMarked(), _refList.ref_type(), 
              referent->hasMultiRef());
        }
      }
    }

  private:  
    void clear_discovered() {
      if (do_cross_test) return;
      RawAccess<>::oop_store_at(_curr_ref, _discovered_off, oop(NULL));
    }

    void clear_referent() {
      if (do_cross_test) return;
      // rtgc_log(LOG_OPT(3), "clear referent ref %p", (void*)_curr_ref);
      RawAccess<>::oop_store_at(_curr_ref, _referent_off, oop(NULL));
    }

  };
};

using namespace RTGC;
oop RefList::g_pending_head = NULL;
oop RefList::g_pending_tail;
int RefList::_referent_off;
int RefList::_discovered_off;
ReferencePolicy* RefList::g_ref_policy = NULL;

static RefList g_softList(REF_SOFT, false);
static RefList g_weakList(REF_WEAK, false);
static RefList g_finalList(REF_FINAL, false);
static RefList g_phantomList(REF_PHANTOM, false);

bool rtHeapEx::g_lock_unsafe_list = false;
bool rtHeapEx::g_lock_garbage_list = false;

void rtHeapEx::initializeRefProcessor() {
  RefList::_referent_off = java_lang_ref_Reference::referent_offset();
  RefList::_discovered_off = java_lang_ref_Reference::discovered_offset();
  g_softList.initialize();
  g_weakList.initialize();
  g_finalList.initialize();
  g_phantomList.initialize();
#ifdef ASSERT  
  g_enqued_referents.initialize();
#endif
  rt_assert(RefList::_referent_off != 0);
  rt_assert(RefList::_discovered_off != 0);
}

// static const char* reference_type_to_string(ReferenceType rt) {
//   switch (rt) {
//     case REF_NONE: return "None ref";
//     case REF_OTHER: return "Other ref";
//     case REF_SOFT: return "Soft ref";
//     case REF_WEAK: return "Weak ref";
//     case REF_FINAL: return "Final ref";
//     case REF_PHANTOM: return "Phantom ref";
//     default:
//       ShouldNotReachHere();
//     return NULL;
//   }
// }

jlong rtHeapEx::_soft_ref_timestamp_clock;

jlong __get_soft_ref_timestamp_clock(bool reset) {
  if (reset) {
    rtHeapEx::_soft_ref_timestamp_clock = java_lang_ref_SoftReference::clock();    
    // rtgc_log(LOG_OPT(3), "reset _soft_ref_timestamp_clock * %lu", rtHeapEx::_soft_ref_timestamp_clock);
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
  // rt_assert_f(soft_ref_clock == _soft_ref_timestamp_clock, "soft ref clocks out of sync");

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
  rtgc_log(LOG_OPT(2), "link_discovered_pending_reference from %p to %p", (void*)ref_q, end);
  oopDesc* discovered;
  for (oopDesc* obj = ref_q; obj != end; obj = discovered) {
    discovered = java_lang_ref_Reference::discovered(obj);
    if (to_obj(obj)->isTrackable()) {
      RTGC::add_trackable_link_or_mark_young_root(discovered, obj);
    } else if (to_obj(discovered)->isTrackable()) {
        rt_assert(true && rtHeap::is_alive(discovered));
      rtHeap::mark_survivor_reachable(discovered);      
    }
  }
}


void rtHeapEx::break_reference_links(ReferencePolicy* policy) {
  GCObject* ref;
  RefList::g_ref_policy = policy;

  for (RefIterator<true> iter(g_weakList); (ref = iter.next_ref(SkipClearedRef_NoGarbageCheck)) != NULL; ) {
    if (ENABLE_SOFT_WEAK_REF && ref->isTrackable()) {
      iter.break_weak_soft_link();
    }
  }

  rtHeapEx::_soft_ref_timestamp_clock = java_lang_ref_SoftReference::clock();
  jlong soft_ref_timestamp = rtHeapEx::_soft_ref_timestamp_clock;
  rtgc_log(LOG_OPT(3), "g_softList 1-1 %d", g_softList._refs.size());
  for (RefIterator<true> iter(g_softList); (ref = iter.next_ref(SkipClearedRef_NoGarbageCheck)) != NULL; ) {
    if (ENABLE_SOFT_WEAK_REF && policy->should_clear_reference(iter.ref(), soft_ref_timestamp)) {
      rtgc_log(LOG_OPT(3), "dirty soft %p tr=%d", ref, ref->isTrackable());
      if (ref->isTrackable()) {
        /* markGarbageOrSometing() 대신에 break_weak_soft_link 를 사용하는 이유.
          Weak/SoftReference 가 referent 를 참조하는 다른 Field를 더 가질 수 있다.
          이에 ref 를 자체를 reverse tracking 하지 못하도록 하는 경우,
          복수의 Field(Reference.referernt + another field) 를 구분하여 처리할 수 없다.
        */
        iter.break_weak_soft_link();
      }
    } else {
      ref->markContextFlag();
    }
  }  
}


void rtHeap::process_weak_soft_references(OopClosure* keep_alive, VoidClosure* complete_gc, bool is_full_gc) {
  // const char* ref_type$ = reference_type_to_string(clear_ref);
  // __process_java_references<REF_NONE, true>(keep_alive, complete_gc);
  // rtgc_log(LOG_OPT(3), "_soft_ref_timestamp_clock * %lu", rtHeapEx::_soft_ref_timestamp_clock);

  jlong soft_ref_timestamp = rtHeapEx::_soft_ref_timestamp_clock;
  // if (is_full_gc) {
  //   ReferencePolicy* policy = RefList::g_ref_policy;
  //   rt_assert(rtHeap::in_full_gc);
    
  //   rtHeap::iterate_younger_gen_roots(NULL, true);
  //   complete_gc->do_void();
  // }


  if (is_full_gc) {
    jlong soft_ref_timestamp = rtHeapEx::_soft_ref_timestamp_clock;
    GCObject* ref;
    rtgc_log(LOG_OPT(1), "g_softList 1-2 %d", g_softList._refs.size());
    for (RefIterator<true> iter(g_softList); (ref = iter.next_ref(DetectGarbageRef)) != NULL; ) {
      if (ref->getContextFlag()) {
        ref->unmarkContextFlag();
        rt_assert(rtHeap::is_alive(iter.referent()));
        // GCObject* referent = to_obj(iter.referent());
        // if (!referent->isTrackable() && !referent_p->is_gc_marked()) {
        //   if (UseCompressedOops) {
        //     keep_alive->do_oop((narrowOop*)iter.referent_addr());
        //   } else {
        //     keep_alive->do_oop((oop*)iter.referent_addr());
        //   }
        // }
      } else {
        rtgc_log(LOG_OPT(3), "clear dirty soft %p tr=%d", ref, ref->isTrackable());
        iter.clear_weak_soft_garbage_referent();
      }
    }
    rtHeap::in_full_gc = -1;

    for (RefIterator<true> iter(g_weakList); (ref = iter.next_ref(DetectGarbageRef)) != NULL; ) {
      if (ENABLE_SOFT_WEAK_REF) {
        iter.clear_weak_soft_garbage_referent();
      }
    }
  }
  
  if (!rtHeap::DoCrossCheck) {
    rtHeapEx::update_soft_ref_master_clock();
  }
}

template <bool is_full_gc>
static bool __keep_alive_young_final_referents(RtYoungRootClosure* closure) {
  GCObject* ref;
  bool changed = false;
  for (RefIterator<is_full_gc> iter(g_finalList); (ref = iter.next_ref(SkipNone)) != NULL; ) {
    oop referent_p = iter.referent();
    GCObject* referent = to_obj(referent_p);
    if (referent->isTrackable() || rtHeap::is_alive(referent_p)) continue;

    rtgc_debug_log(referent, "keep alive ref %p -> %p", ref, referent);
    oop ref_p = cast_to_oop(ref);
    referent->unmarkActiveFinalizerReachable();
    ref->unmarkActiveFinalizer();

    oop new_p = closure->keep_alive_young_referent(referent_p);
    if (!is_full_gc) {
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_p, RefList::_referent_off, referent_p);
    } else {
      rt_assert(!rtHeap::is_in_trackable_space(new_p));
    }
    
    if (!ref->isTrackable()) {
      // final referent 는 unstable reachable 이 아니다.
      // rtHeap::mark_young_survivor_reachable(ref_p, referent_p);
    } else if (!is_full_gc && rtHeap::is_in_trackable_space(new_p)) {
      rtHeap::add_trackable_link(ref_p, referent_p);
    } else {
      // 불필요.
      // rtHeap::mark_young_root_reachable(ref_p, referent_p);
    }
    iter.enqueue_curr_ref(false);      
    changed = true;
  }
  if (changed) {
    rtgc_log(true, "complete marking final referent followers");
    closure->do_complete(true);
  }
  return changed;
}
bool rtHeapEx::keep_alive_young_final_referents(RtYoungRootClosure* closure, bool is_full_gc) {
  if (is_full_gc) {
    return __keep_alive_young_final_referents<true>(closure);
  } else {
    return __keep_alive_young_final_referents<false>(closure);
  }
}


template<typename T, bool is_full_gc>
static void __keep_alive_final_referents(OopClosure* keep_alive, VoidClosure* complete_gc) {
  GCObject* ref;
  for (RefIterator<is_full_gc> iter(g_finalList); (ref = iter.next_ref(SkipNone)) != NULL; ) {
    GCObject* referent = to_obj(iter.referent());
    bool is_alive;
    if (referent->isTrackable()) {
      is_alive = _rtgc.g_pGarbageProcessor->resolveStrongSurvivalPath(referent);
    } else {
      is_alive = cast_to_oop(referent)->is_gc_marked();
      rt_assert(is_full_gc || is_alive);
    }

    if (!is_full_gc) {
      iter.adjust_ref_pointer();
      ref = to_obj(iter.ref());
      // ActiveFinalizer/Phantom Reference 은 scan 예외 대상이다.
      rt_assert_f((void*)referent == iter.get_raw_referent(), 
          "ref %p new_ref %p" PTR_DBG_SIG PTR_DBG_SIG, (void*)referent, iter.get_raw_referent(),
          PTR_DBG_INFO(referent), PTR_DBG_INFO(iter.get_raw_referent()));
    } else if (rtHeap::DoCrossCheck && referent->isTrackable()) {
      bool is_gc_marked = cast_to_oop(referent)->is_gc_marked();
      rt_assert(!referent->isGarbageMarked());
      rt_assert_f(is_gc_marked == is_alive, 
          "damaged referent %p(%s) gc_mark=%d rc=%d, unsafe=%d ac=%d garbage=%d ghost=%d", 
          referent, RTGC::getClassName(referent), is_gc_marked, referent->getRootRefCount(), 
          referent->isUnstableMarked(), 
          referent->getAnchorCount(), referent->isGarbageMarked(), rtHeapEx::print_ghost_anchors(referent));
    }
    
    if (!is_alive) {
      rt_assert(is_full_gc || referent->isTrackable());
      if (true) {
        rtgc_log(LOG_OPT(3), "resurrect final ref %p of %p", ref, referent);
        if (is_full_gc) {
          MarkSweep::_is_rt_anchor_trackable = ref->isTrackable();
        }
        if (!referent->isTrackable() || (rtHeap::DoCrossCheck && is_full_gc)) {
          keep_alive->do_oop((T*)iter.referent_addr());
        } else {
          rt_assert(!referent->isGarbageMarked());
        }
      }
      GCObject* old_referent = referent;
      if (is_full_gc) {
        rt_assert(referent == (void*)iter.get_raw_referent());
        referent->unmarkActiveFinalizerReachable();
      } else {
        referent = to_obj(iter.get_raw_referent());
        referent->unmarkActiveFinalizerReachable();
      }
      /* referent 가 순환 가비지의 일부이면, referrerList.size()가 0보다 크다 */
      rt_assert(!referent->isGarbageMarked());
      rt_assert(!referent->hasSafeAnchor() || referent->getSafeAnchor() != ref);
      rt_assert(!referent->hasShortcut());
      rt_assert_f(!referent->isTrackable() || referent->getRootRefCount() == 0, "rc = %d", referent->getRootRefCount());
      if (rtHeap::DoCrossCheck) {
        rt_assert(cast_to_oop(old_referent)->is_gc_marked() || (!is_full_gc && old_referent->isTrackable()));
      }
      rt_assert(!referent->isActiveFinalizerReachable());
      rtgc_log(LOG_OPT(2), "final ref cleared 1 %p -> %p(%p)(%s)", 
          (void*)ref, old_referent, referent, RTGC::getClassName(old_referent));
      if (ref->isTrackable()) {
        // ref-count -> ref-link 로 변환. 
        // RTGC-TODO Finalizable 객체 생성 시 acyclic marking 후, pending Q에 넣기 전에 acyclic 해제.
        RTGC::add_trackable_link_or_mark_young_root(cast_to_oop(referent), cast_to_oop(ref));
        if (referent->isTrackable() && !referent->hasSafeAnchor()) {
          referent->setSafeAnchor(ref);
          referent->setShortcutId_unsafe(INVALID_SHORTCUT);
        }
      } else if (referent->isTrackable()) {
        // gc 종료 후 Unsafe List 등록되도록 한다.
        rtgc_log(LOG_OPT(3), "yg-reachable final referent %p", referent);
        rt_assert(true && rtHeap::is_alive(cast_to_oop(referent)));
        rtHeap::mark_survivor_reachable(cast_to_oop(referent));
      }
      ref->unmarkActiveFinalizer();
      iter.enqueue_curr_ref(false);
    } else {
      rtgc_log(LOG_OPT(3), "active final ref %p of %p(%s)", ref, referent, getClassName(referent));
      if (!is_full_gc) {
        iter.adjust_referent_pointer();
      }
      rt_assert_f(referent->isActiveFinalizerReachable(), "must be ActiveFinalizerReachable %p", referent);
    }
  }
  complete_gc->do_void();
  RefList::hold_pending_q();
  rtHeap__clear_garbage_young_roots(is_full_gc);
  if (is_full_gc) {
    rt_assert(!_rtgc.g_pGarbageProcessor->hasUnsafeObjects());
    rtHeapEx::g_lock_unsafe_list = true;
  } else {
    rtHeapEx::adjust_ref_q_pointers(false);
  }
}


void rtHeapEx::keep_alive_final_referents(RefProcProxyTask* proxy_task) {
  SerialGCRefProcProxyTask* task = (SerialGCRefProcProxyTask*)proxy_task;
  OopClosure* keep_alive = task->keep_alive_closure();
  VoidClosure* complete_gc = task->complete_gc_closure();

        rtgc_log(LOG_OPT(1), "keep_alive_final_referents %p", proxy_task);

  if (rtHeap::in_full_gc && rtHeap::DoCrossCheck) {
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
  rt_assert(!is_full_gc || !_rtgc.g_pGarbageProcessor->hasUnsafeObjects());

        rtgc_log(LOG_OPT(1), "__process_final_phantom_references");

  for (RefIterator<is_full_gc> iter(g_phantomList); iter.next_ref(SkipInvalidRef) != NULL; ) {
    oopDesc* old_referent = iter.referent();
    rt_assert(old_referent != NULL);
    bool is_alive = rtHeap::is_alive(old_referent, false);
    if (!is_full_gc) {
      iter.adjust_ref_pointer();
    }

    if (!is_alive) {
      rt_assert(!to_obj(old_referent)->isTrackable() || to_obj(old_referent)->isDestroyed());
      iter.enqueue_curr_ref(true);
    } else if (!is_full_gc) {
      iter.adjust_referent_pointer();
    }
    rtgc_log(LOG_OPT(5), 
      "check phantom ref) %p(alive=%d) of %p -> %p", iter.ref(), is_alive, old_referent, iter.referent());
  } 

  RefList::flush_penging_list(is_full_gc);

  rt_assert(!is_full_gc || !_rtgc.g_pGarbageProcessor->hasUnsafeObjects());
}

void rtHeap::process_final_phantom_references(OopClosure* keep_alive, VoidClosure* complete_gc, bool is_tenure_gc) {
  if (!rtHeap::DoCrossCheck) {
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

  if (is_tenure_gc) {
    __process_final_phantom_references<true>();
  } else {
    __process_final_phantom_references<false>();
  }
}




template<bool is_full_gc>
void __adjust_ref_q_pointers() {
  rtgc_log(LOG_OPT(1), "__adjust_ref_q_pointers");
  /* 2023.0318 Full-GC 시에는 이미 Garbage 객체가 모두 제거된 상태이다. 
     특히, YG-객체 중 위치가 이동되지 않은 객체는 gc_mark 가 해제된 상태라 가비지 검사가 불가능하다.
     참고) sh exec_test.sh gc/serial/HeapChangeLogging
  */  
  SkipPolicy soft_weak_policy = is_full_gc ? /*SkipGarbageRef_*/NoReferentCheck : SkipInvalidRef;
  rtgc_log(LOG_OPT(3), "g_softList 2 %d", g_softList._refs.size());
  for (RefIterator<is_full_gc> iter(g_softList); iter.next_ref(soft_weak_policy) != NULL; ) {
    rt_assert(!to_obj(iter.ref())->isGarbageMarked());
    iter.adjust_ref_pointer();
    if (!is_full_gc && to_obj(iter.ref())->isGarbageMarked()) {
      // RTGC-TODO resurrection 없엔 후 아래 코드 제거.
      // YG-root 내부의 객체가 marking 시 promote 된 후, 해당 YG-root 가 garbage 처리된 상황.
      rtgc_log(LOG_OPT(3), "promoted garbage soft-ref %p detected", to_obj(iter.ref()));
      iter.remove_curr_ref(false);
    }
  } 
  rtgc_log(LOG_OPT(3), "g_weakList 2 %d", g_weakList._refs.size());
  for (RefIterator<is_full_gc> iter(g_weakList); iter.next_ref(soft_weak_policy) != NULL; ) {
    rt_assert(!to_obj(iter.ref())->isGarbageMarked());
    iter.adjust_ref_pointer();
    if (!is_full_gc && to_obj(iter.ref())->isGarbageMarked()) {
      // RTGC-TODO resurrection 없엔 후 아래 코드 제거.
      // YG-root 내부의 객체가 marking 시 promote 된 후, 해당 YG-root 가 garbage 처리된 상황.
      rtgc_log(LOG_OPT(3), "promoted garbage weak-ref %p detected", to_obj(iter.ref()));
      iter.remove_curr_ref(false);
    }
  } 

#ifdef ASSERT
  for (RefIterator<is_full_gc> iter(g_finalList); iter.next_ref(NoGarbageCheck) != NULL; ) {
    rt_assert(to_obj(iter.referent())->isActiveFinalizerReachable());
  } 
#endif

  if (is_full_gc) {
    rtgc_log(LOG_OPT(3), "g_finalList 2 %p", g_finalList._ref_q);
    for (RefIterator<is_full_gc> iter(g_finalList); iter.next_ref(SkipNone) != NULL; ) {
      iter.adjust_ref_pointer();
      iter.adjust_referent_pointer();
    } 

    rtgc_log(LOG_OPT(3), "g_phantomList 2 %p", g_phantomList._ref_q);
    for (RefIterator<is_full_gc> iter(g_phantomList); iter.next_ref(SkipNone) != NULL; ) {
      // oopDesc* old_p = iter.referent();
      iter.adjust_ref_pointer();
      iter.adjust_referent_pointer();
      // rtgc_log(LOG_OPT(5), 
      //   "active phantom ref) %p of %p -> %p", iter.ref(), old_p, iter.referent());
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
  rt_assert(rtHeap::DoCrossCheck);
  rtgc_log(LOG_OPT(3), "enq ref(%p) of %p", ref_p, referent_p);
  int idx = g_enqued_referents.indexOf(ref_p);
  rt_assert_f(idx >= 0, "ref[%d] %p of %p not found", __getRefType(ref_p),  ref_p, referent_p);
  g_enqued_referents.removeFast(idx);
}


bool rtHeap::is_referent_reachable(oopDesc* ref, ReferenceType type) {
  switch (type) {
    case REF_PHANTOM: 
      return false;
    case REF_FINAL: 
      return !to_obj(ref)->isActiveFinalizer();
    default:
      return true;
  }
}

bool rtHeap::try_discover(oopDesc* ref, ReferenceType type, ReferenceDiscoverer* refDiscoverer) {
  rt_assert(EnableRTGC);
  switch (type) {
    case REF_PHANTOM: {
      // GC 두 번 이상 수행한 이후에 Reference.getAndClearReferencePendingList() 가 호출되는 경우가 있다.
      // 이 때, Universe::reference_pending_list 에 대한 marking 및 pointer adjust 처리를 위해
      // referent 에 대한 검사가 필요하다.
      oop referent = RawAccess<>::oop_load_at(ref, RefList::_referent_off);
      return referent != NULL;
    }
    case REF_FINAL: 
      return to_obj(ref)->isActiveFinalizer();

    case REF_WEAK:
      if (!rtHeap::in_full_gc || !ENABLE_SOFT_WEAK_REF) {
        return false;
      } else {
        oop referent = RawAccess<>::oop_load_at(ref, RefList::_referent_off);
        return referent != NULL;// && referent->is_gc_marked();
      }

    case REF_SOFT:
      if (rtHeap::in_full_gc <= 0) {
        return false;
      } else {
        return ENABLE_SOFT_WEAK_REF && !to_obj(ref)->getContextFlag();
        // oop referent = RawAccess<>::oop_load_at(ref, RefList::_referent_off);
        // return referent != NULL 
        //     && RefList::g_ref_policy->should_clear_reference(ref, rtHeapEx::_soft_ref_timestamp_clock);
      }

    default:
      fatal("should not reach here!");
      return false;
  }
}

void rtHeap::init_java_reference(oopDesc* ref, oopDesc* referent_p) {
  rt_assert(RtNoDiscoverPhantom);
  rt_assert(referent_p != NULL);

  rtgc_debug_log(referent_p, "init_java_reference %p -> %p", ref, referent_p);
  ReferenceType refType = InstanceKlass::cast(ref->klass())->reference_type();
  switch (refType) {
    case REF_PHANTOM:
      g_phantomList.register_ref(ref, referent_p);
      return;

    case REF_FINAL:
      to_obj(ref)->markActiveFinalizer();
      to_obj(referent_p)->markActiveFinalizerReachable();
      g_finalList.register_ref(ref, referent_p);
      return;

    case REF_WEAK:
      g_weakList.register_ref(ref, referent_p);
      break;
    
    case REF_SOFT:
      g_softList.register_ref(ref, referent_p);
      break;

    default:
      fatal("should not reach here.");
  }

  if (to_obj(ref)->isTrackable()) {
    // 참고) JNI 함수 호출 도중에 GC가 발생한 경우, ref 가 trackble 상태일 수 있다.
    rtgc_log(LOG_OPT(2), "weird ref %p of %p", (void*)ref, (void*)referent_p);
    RTGC::lock_heap();
    RTGC::add_trackable_link_or_mark_young_root(referent_p, ref);
    RTGC::unlock_heap();
  }

  return;
}

