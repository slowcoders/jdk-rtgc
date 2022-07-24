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

  RtRefProcessor() { _ref_q = NULL; }

  template <bool is_full_gc>
  void process_references(OopClosure* keep_alive);

  void register_pending_refereneces();

  template <bool is_full_gc>
  oopDesc* get_valid_forwardee(oopDesc* obj);

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
    if (refType != REF_PHANTOM) return obj;

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
    rtgc_log(LOG_OPT(3), "check %s %p\n", ref_type, (void*)ref_op);
    next_ref_op = RawAccess<>::oop_load_at(ref_op, discovered_off);
    precond(ref_op != next_ref_op);
    debug_only(cnt_ref++;)

    oop ref_np = get_valid_forwardee<is_full_gc>(ref_op);
    if (ref_np == NULL) {
      rtgc_log(LOG_OPT(3), "garbage %s %p removed\n", ref_type, (void*)ref_op);
      debug_only(cnt_garbage++;)
      continue;
    }

    rtgc_log(LOG_OPT(3), "%s %p moved %p\n", ref_type, (void*)ref_op, (void*)ref_np);
    oop referent_op = RawAccess<>::oop_load_at(ref_op, referent_off);
    precond(referent_op != NULL);
    acc_ref = is_full_gc ? ref_op : ref_np;
    if (referent_op == ref_op) {
      debug_only(cnt_cleared++;)
      /**
       * Two step referent clear (to hide discoverd-link)
       * 참고) referent 값이 null 이 되면, discovered 가 normal-ref 참조로 처리된다.
       *      이에, reference-queue 에 제거한 후, referent 값을 null 로 변경한다.
       * See java_lang_ref_Reference::clear_referent().
       */
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, referent_off, oop(NULL));
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, discovered_off, oop(NULL));
      rtgc_log(LOG_OPT(3), "referent cleaned %p (maybe by PhantomCleanable)\n", (void*)ref_op);
      continue;
    }

    oop referent_np = get_valid_forwardee<is_full_gc>(referent_op);
    if (referent_np == NULL) {
      debug_only(cnt_pending++;)
      if (refType == REF_FINAL) {
        GCObject* node = to_obj(referent_op);
        precond(node->getRootRefCount() == 1);
        GCRuntime::onEraseRootVariable_internal(node);
        referent_np = referent_op;
        keep_alive->do_oop((oop*)&referent_np);
      }

      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, referent_off, referent_np);
      rtgc_log(LOG_OPT(3), "reference %p(->%p) with garbage referent linked after (%p)\n", 
            (void*)ref_op, (void*)ref_np, (void*)pending_tail_acc);

      if (pending_head == NULL) {
        precond(pending_tail_acc == NULL);
        pending_head = acc_ref;
      } else {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail_acc, discovered_off, acc_ref);
        if (to_node(pending_tail_acc)->isTrackable()) {
          RTGC::add_referrer_ex(acc_ref, pending_tail_acc, true);
        }
      }
      pending_tail_acc = acc_ref;
    } else {
      rtgc_log(LOG_OPT(3), "alive reference %p(->%p) linked (%p)\n", 
            (void*)ref_op, (void*)ref_np, (void*)alive_head);
      java_lang_ref_Reference::set_discovered_raw(acc_ref, alive_head);
      alive_head = ref_np;
      debug_only(cnt_alive++;)

      rtgc_log(LOG_OPT(3), "referent of (%p) marked %p -> %p\n", (void*)ref_op, (void*)referent_op, (void*)referent_np);
      if (referent_op != referent_np) {
        HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, referent_off, referent_np);
      }
    }   
  } 

  _ref_q = alive_head;
  rtgc_log(true || LOG_OPT(3), "total %s scanned %d, garbage %d, cleared %d, pending %d, alive %d q=%p\n",
        ref_type, cnt_ref, cnt_garbage, cnt_cleared, cnt_pending, cnt_alive, (void*)alive_head);

  if (pending_head != NULL) {
    oopDesc* enqueued_top_np = Universe::swap_reference_pending_list(pending_head);
    HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail_acc, discovered_off, enqueued_top_np);
    if (enqueued_top_np != NULL && to_node(pending_tail_acc)->isTrackable()) {
      RTGC::add_referrer_ex(enqueued_top_np, pending_tail_acc, true);
    }
  }
}


void rtHeap::init_java_reference(oopDesc* ref, oopDesc* referent) {
  precond(RtNoDiscoverPhantom);
  precond(referent != NULL);

  ptrdiff_t referent_offset = java_lang_ref_Reference::referent_offset();
  oopDesc** ref_q;
  switch (InstanceKlass::cast(ref->klass())->reference_type()) {
    // case REF_FINAL:
    //   ref_q = &g_finalRefProcessor._ref_q;
    //   GCRuntime::onAssignRootVariable_internal(to_obj(referent));
    //   break;
    case REF_PHANTOM:
      ref_q = &g_phantomRefProcessor._ref_q;
      break;

    default:
      HeapAccess<>::oop_store_at(ref, referent_offset, referent);
      rtgc_log(false && ((InstanceRefKlass*)ref->klass())->reference_type() == REF_WEAK, 
            "weak ref %p for %p\n", (void*)ref, referent);
      return;
  }

  rtgc_log(LOG_OPT(3), "created ref %p for %p\n", (void*)ref, referent);
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref, referent_offset, referent);
  oop next_discovered = Atomic::xchg(ref_q, ref);
  java_lang_ref_Reference::set_discovered_raw(ref, next_discovered);
  return;
}


void rtHeap::link_discovered_pending_reference(oopDesc* ref_q, oopDesc* end) {
  precond(!java_lang_ref_Reference::is_final(ref_q));
  precond(!java_lang_ref_Reference::is_phantom(ref_q));
  oopDesc* discovered;
  for (oopDesc* obj = ref_q; obj != end; obj = discovered) {
    discovered = java_lang_ref_Reference::discovered(obj);
    if (to_obj(obj)->isTrackable()) {
      RTGC::add_referrer_ex(discovered, obj, true);
    }
  }
}

void rtHeap::process_java_references(OopClosure* keep_alive, bool is_full_gc) {
  if (is_full_gc) {
    g_finalRefProcessor.process_references<true>(keep_alive);
  } else {
    g_finalRefProcessor.process_references<false>(keep_alive);
  }
}

void rtHeapEx::clear_phantom_references(bool is_full_gc) {
  if (is_full_gc) {
    g_phantomRefProcessor.process_references<true>(NULL);
  } else {
    g_phantomRefProcessor.process_references<false>(NULL);
  }
}

