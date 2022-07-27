#include "gc/rtgc/rtRefProcessor.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace rtHeapUtil;
using namespace RTGC;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

template <ReferenceType refType>
class RtRefProcessor {
public:
  oopDesc* _ref_q;
  oopDesc* _pending_q;

  RtRefProcessor() { _ref_q = NULL; _pending_q = NULL; }

  template <bool is_full_gc>
  void process_references(OopClosure* keep_alive);

  template <bool is_full_gc>
  void add_pending_references(oopDesc* pending_head, oopDesc* pending_tail_acc);

  template <bool is_full_gc>
  oopDesc* get_valid_forwardee(oopDesc* obj);

  void adjust_ref_q_pointers();

  void link_pending_reference(oopDesc* anchor, int discovered_off, oopDesc* link);

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

static RtRefProcessor<REF_FINAL>   g_finalRefProcessor;
static RtRefProcessor<REF_PHANTOM> g_phantomRefProcessor;

template <ReferenceType refType>
template <bool is_full_gc>
oopDesc* RtRefProcessor<refType>::get_valid_forwardee(oopDesc* obj) {
  if (is_full_gc) {
    //if (refType != REF_PHANTOM) {
      return obj->is_gc_marked() ? obj : NULL;
    //}

    if (to_obj(obj)->isGarbageMarked()) {
      assert(!obj->is_gc_marked() || is_dead_space(obj), "wrong garbage mark on %p()\n", 
          obj);//, RTGC::getClassName(to_obj(obj)));
      return NULL;
    } else {
      assert(obj->is_gc_marked(), "must be gc_marked %p()\n", 
          obj);//, RTGC::getClassName(to_obj(obj)));
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

template <ReferenceType refType>
template <bool is_full_gc>
void RtRefProcessor<refType>::process_references(OopClosure* keep_alive) {
#ifdef ASSERT  
  int cnt_garbage = 0;
  int cnt_ref = 0;
  int cnt_pending = 0;
  int cnt_cleared = 0;
  int cnt_alive = 0;
  const char* ref_type = reference_type_to_string(refType);
#endif


  oop acc_ref = NULL;
  oop next_ref_op;
  oop alive_head = NULL;
  oopDesc* pending_tail_acc = NULL;
  oopDesc* pending_head = NULL;
  const int referent_off = java_lang_ref_Reference::referent_offset();
  const int discovered_off = java_lang_ref_Reference::discovered_offset();

  for (oop ref_op = _ref_q; ref_op != NULL; ref_op = next_ref_op) {
    // rtgc_log(LOG_OPT(3), "check %s %p\n", ref_type, (void*)ref_op);
    next_ref_op = RawAccess<>::oop_load_at(ref_op, discovered_off);
    precond(ref_op != next_ref_op);
    debug_only(cnt_ref++;)

    oop acc_ref = get_valid_forwardee<is_full_gc>(ref_op);
    if (acc_ref == NULL) {
      // final reference 는 referent 보다 먼저 삭제될 수 없다.
      precond(refType != REF_FINAL);
      rtgc_log(LOG_OPT(3), "garbage %s %p removed\n", ref_type, (void*)ref_op);
      debug_only(cnt_garbage++;)
      continue;
    }

    // rtgc_log(LOG_OPT(3), "%s %p moved %p\n", ref_type, (void*)ref_op, (void*)acc_ref);
    oop referent_op = RawAccess<>::oop_load_at(ref_op, referent_off);
    precond(referent_op != NULL);
    if (referent_op == ref_op) {
      debug_only(cnt_cleared++;)
      /**
       * Two step referent clear (to hide discoverd-link)
       * 참고) referent 값이 null 이 되면, discovered 가 normal-ref 참조로 처리된다.
       *      이에, reference-queue 에 제거한 후, referent 값을 null 로 변경한다.
       * See java_lang_ref_Reference::clear_referent().
       */
      precond(refType != REF_FINAL);
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, referent_off, oop(NULL));
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, discovered_off, oop(NULL));
      rtgc_log(LOG_OPT(3), "referent cleaned %p (maybe by PhantomCleanable)\n", (void*)acc_ref);
      continue;
    }

    oop acc_referent = get_valid_forwardee<is_full_gc>(referent_op);
    if (acc_referent == NULL) {
      debug_only(cnt_pending++;)
      if (refType == REF_FINAL) {
        acc_referent = referent_op;
        keep_alive->do_oop((oop*)&acc_referent);
        postcond(referent_op->is_gc_marked());
        precond(!is_full_gc || acc_referent == referent_op);

        GCObject* node = to_obj(acc_referent);
        precond(node->isActiveRef());
        node->unmarkActiveRef();
        postcond(node->getRootRefCount() == 0);
        if (to_obj(acc_ref)->isTrackable()) {
          RTGC::add_referrer_ex(acc_referent, acc_ref, true);
        }
      }
      if (!is_full_gc || refType != REF_FINAL) {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, referent_off, acc_referent);
      }

      rtgc_log(LOG_OPT(3), "reference %p(->%p tr;%d) with garbage referent linked after (%p)\n", 
            (void*)ref_op, (void*)acc_ref, to_obj(acc_ref)->isTrackable(), (void*)pending_tail_acc);

      if (pending_head == NULL) {
        precond(pending_tail_acc == NULL);
        pending_head = acc_ref;
      } else {
        link_pending_reference(pending_tail_acc, discovered_off, acc_ref);
        // HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail_acc, discovered_off, acc_ref);
        // if (to_node(pending_tail_acc)->isTrackable()) {
        //   RTGC::add_referrer_ex(acc_ref, pending_tail_acc, true);
        // }
      }
      pending_tail_acc = acc_ref;
    } else {
      java_lang_ref_Reference::set_discovered_raw(acc_ref, alive_head);
      alive_head = acc_ref;
      debug_only(cnt_alive++;)

      rtgc_log(LOG_OPT(3), "referent of (%p->%p) marked %p -> %p\n", (void*)ref_op, (void*)acc_ref, (void*)referent_op, (void*)acc_referent);
      if (!is_full_gc || referent_op != acc_referent) {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, referent_off, acc_referent);
      }
    }   
  } 

  rtgc_log(LOG_OPT(3), "total %s scanned %d, garbage %d, cleared %d, pending %d, active %d q=%p\n",
        ref_type, cnt_ref, cnt_garbage, cnt_cleared, cnt_pending, cnt_alive, (void*)alive_head);

  _ref_q = alive_head;
  if (pending_head != NULL) {
    oopDesc* enqueued_top_np = Universe::swap_reference_pending_list(pending_head);
    link_pending_reference(pending_tail_acc, discovered_off, enqueued_top_np);
    // add_pending_references<is_full_gc>(pending_head, pending_tail_acc);
  }
}

template <ReferenceType refType>
void RtRefProcessor<refType>::link_pending_reference(oopDesc* anchor, int discovered_off, oopDesc* link) {
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(anchor, discovered_off, link);
  if (link != NULL && to_node(anchor)->isTrackable()) {
    precond(!to_obj(link)->isGarbageMarked());
    RTGC::add_referrer_ex(link, anchor, true);
  }
}

template <ReferenceType refType>
template <bool is_full_gc>
void RtRefProcessor<refType>::add_pending_references(oopDesc* pending_head, oopDesc* pending_tail_acc) {
  const int discovered_off = java_lang_ref_Reference::discovered_offset();
  oopDesc* enqueued_top_np = Universe::swap_reference_pending_list(pending_head);
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail_acc, discovered_off, enqueued_top_np);
  if (enqueued_top_np != NULL && to_node(pending_tail_acc)->isTrackable()) {
    precond(!to_obj(enqueued_top_np)->isGarbageMarked());
    RTGC::add_referrer_ex(enqueued_top_np, pending_tail_acc, true);
  }
}

template <ReferenceType refType>
void RtRefProcessor<refType>::adjust_ref_q_pointers() {
  const char* ref_type = reference_type_to_string(refType);
  oopDesc* prev_ref_op = NULL;
  oop next_ref_op;
  const int referent_off = java_lang_ref_Reference::referent_offset();
  const int discovered_off = java_lang_ref_Reference::discovered_offset();
  for (oop ref_op = _ref_q; ref_op != NULL; ref_op = next_ref_op) {
    next_ref_op = RawAccess<>::oop_load_at(ref_op, discovered_off);

    oop acc_ref = ref_op->forwardee();
    if (acc_ref == NULL) acc_ref = ref_op;
    precond(ref_op != next_ref_op);

    oop referent_op = RawAccess<>::oop_load_at(ref_op, referent_off);
    oop acc_referent = referent_op->forwardee();
    if (acc_referent == NULL) acc_referent = referent_op;
    if (referent_op != acc_referent) {
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_op, referent_off, acc_referent);
    }

    if (prev_ref_op != NULL) {
      java_lang_ref_Reference::set_discovered_raw(prev_ref_op, acc_ref);
    } else {
      _ref_q = acc_ref;
    }
    prev_ref_op = ref_op;
  } 
}

void rtHeap::init_java_reference(oopDesc* ref, oopDesc* referent) {
  precond(RtNoDiscoverPhantom);
  precond(referent != NULL);

  ptrdiff_t referent_offset = java_lang_ref_Reference::referent_offset();
  oopDesc** ref_q;
  switch (InstanceKlass::cast(ref->klass())->reference_type()) {
    case REF_PHANTOM:
      ref_q = &g_phantomRefProcessor._ref_q;
      rtgc_log(LOG_OPT(3), "created Phantom ref %p for %p\n", (void*)ref, referent);
      break;

    case REF_FINAL:
      to_obj(referent)->markActiveRef();
      ref_q = &g_finalRefProcessor._ref_q;
      rtgc_log(LOG_OPT(3), "created Final ref %p for %p\n", (void*)ref, referent);
      break;

    default:
      HeapAccess<>::oop_store_at(ref, referent_offset, referent);
      rtgc_log(false && ((InstanceRefKlass*)ref->klass())->reference_type() == REF_WEAK, 
            "weak ref %p for %p\n", (void*)ref, referent);
      return;
  }

  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, referent_offset, referent);
  oop next_discovered = Atomic::xchg(ref_q, ref);
  java_lang_ref_Reference::set_discovered_raw(ref, next_discovered);
  return;
}


void rtHeap::link_discovered_pending_reference(oopDesc* ref_q, oopDesc* end) {
  // precond(!java_lang_ref_Reference::is_final(ref_q));
  // precond(!java_lang_ref_Reference::is_phantom(ref_q));
  rtgc_log(LOG_OPT(3), "link_discovered_pending_reference from %p to %p\n", (void*)ref_q, end);
  oopDesc* discovered;
  for (oopDesc* obj = ref_q; obj != end; obj = discovered) {
    discovered = java_lang_ref_Reference::discovered(obj);
    if (to_obj(obj)->isTrackable()) {
      RTGC::add_referrer_ex(discovered, obj, true);
    }
  }
}

void rtHeap::process_java_references(OopClosure* keep_alive, VoidClosure* complete_gc, bool is_full_gc) {
  if (is_full_gc) {
    g_finalRefProcessor.process_references<true>(keep_alive);
    complete_gc->do_void();
    g_phantomRefProcessor.process_references<true>(NULL);
  } else {
    g_finalRefProcessor.process_references<false>(keep_alive);
    complete_gc->do_void();
    g_phantomRefProcessor.process_references<false>(NULL);
  }
}

bool rtHeap::can_discover(oopDesc* final_referent) {
  return !to_obj(final_referent)->isActiveRef();
}

void rtHeapEx::adjust_ref_q_pointers(bool is_full_gc) {
  if (is_full_gc) {
    g_finalRefProcessor.adjust_ref_q_pointers();
    g_phantomRefProcessor.adjust_ref_q_pointers();
  } else {
  }
}

