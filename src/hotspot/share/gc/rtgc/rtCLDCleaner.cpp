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

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

#include "rtSpace.hpp"
#include "rtCLDCleaner.hpp"

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

using namespace RTGC;

namespace rtCLDCleaner {
  debug_only(static int idx_cld = 0;)

  enum CldHandleAction {
    ClearHandle,
    RemarkHandle,
    MarkUntrackable,
  };

  static bool g_need_rescan_cld = false;
  static ClassLoaderData* tenured_class_loader_data(oop obj, bool ignoreMirrorClass=true);

  template<CldHandleAction action>
  class CLDHandleClosure : public OopClosure {
  public:
    debug_only(int _idxHandle;)

    CLDHandleClosure() {
      debug_only(_idxHandle = 0;)
    }

    virtual void do_oop(oop* o) { do_oop_work(o); }
    virtual void do_oop(narrowOop* o) { fatal("cld handle not compressed"); }

    void do_oop_work(oop* o) {
      oop obj = *o;
      debug_only(_idxHandle ++;)
      if (obj != NULL) {
        GCObject* node = to_obj(obj);
        if (action == ClearHandle) {
#ifdef ASSERT          
          if (false && idx_cld == 12 && _idxHandle == 2) {
            rtgc_log(true, "ClearHandle %p(%s) cld=[%d] tr=%d\n", 
              node, RTGC::getClassName(node), idx_cld, node->isTrackable());
            RTGC::debug_obj = node;
          }
#endif          
          precond(!node->isTrackable() || rtHeap::is_alive(obj));
          rtHeap::release_jni_handle(obj);
          return;
        }

        if (action == RemarkHandle) {
          if (node->isGarbageMarked()) {
            // CLD 가 계층적으로 참조된 경우, 하위 CLD 에 대한 detectGarbage 에 의해
            // 상위 handle이 garbageMarking 된 상태가 될 수 있다. 
            assert(node->isTrackable(), "invalid handle %p(%s) cld=[%d] idx=%d rc=%d\n", 
                node, RTGC::getClassName(node), idx_cld, _idxHandle, node->getReferrerCount());
            rtHeapUtil::resurrect_young_root(node);
          }
          rtHeap::lock_jni_handle(obj);
        }

        postcond((node->getRootRefCount() & 0x3FF) != 0);
        if (!node->isTrackable() && !obj->is_gc_marked()) {
          MarkSweep::mark_and_push_internal(obj, false);
        }
      }
    }
  }; 

  template<bool mark_ref>
  class WeakCLDScanner : public CLDClosure, public KlassClosure {
    bool _is_alive;
    int _cnt_handle;
    HugeArray<GCObject*>* _garbages;
  public:
    void init() {
      _garbages = _rtgc.g_pGarbageProcessor->getGarbageNodes();
    }

    void do_cld(ClassLoaderData* cld) {
      debug_only(idx_cld ++;)
      if (rtHeap::DoCrossCheck) {
        if (mark_ref) {
          oopDesc* holder = cld->holder_no_keepalive();
          precond(holder != NULL);
          if (!holder->is_gc_marked()) {
            bool is_garbage = _rtgc.g_pGarbageProcessor->detectGarbage(to_obj(holder));
            assert(is_garbage, 
                "alive holder %p(%s) isClass=%d rc=%d\n",
                holder, RTGC::getClassName(to_obj(holder)), 
                holder->klass() == vmClasses::Class_klass(), to_obj(holder)->getRootRefCount());
            return;
          }
          precond(rtHeap::is_alive(holder));
        }
        CLDHandleClosure<mark_ref ? RemarkHandle : ClearHandle> closure;
        cld->oops_do(&closure, ClassLoaderData::_claim_none);
        return;
      }

      oopDesc* holder = cld->holder_no_keepalive();
      assert(holder != NULL, "WeakCLDScanner must be called befor weak-handle cleaning.");
      
      if (!mark_ref) {
        if (cld->holder_ref_count() > 0 || cld->class_loader() == NULL ||
          (to_obj(holder)->isTrackable() ? to_obj(holder)->getRootRefCount() > 2 : holder->is_gc_marked())) {
          // rtgc_log(LOG_OPT(2), "skip cld scanner %p/%p cld_rc=%d, rc=%d tr=%d\n", 
          //     cld, holder, cld->holder_ref_count(), to_obj(holder)->getRootRefCount(), to_obj(holder)->isTrackable());
          cld->increase_holder_ref_count();
          CLDHandleClosure<MarkUntrackable> marker;
          cld->oops_do(&marker, ClassLoaderData::_claim_none);
        } else {
          rtgc_log(LOG_OPT(2), "cleaning cld %p/%p cld_rc=%d, rc=%d tr=%d\n", 
              cld, holder, cld->holder_ref_count(), to_obj(holder)->getRootRefCount(), to_obj(holder)->isTrackable());
          CLDHandleClosure<ClearHandle> cleaner;
          cld->oops_do(&cleaner, ClassLoaderData::_claim_none);
        }
        return;
      }

      if (cld->holder_ref_count() > 0) {
        postcond(rtHeap::is_alive(holder));
        cld->decrease_holder_ref_count();
        return;
      }

      int top = _garbages->size();
      if (has_any_alive_klass(cld, holder)) {
        rtgc_log(LOG_OPT(2), "remark cld handles %p/%p garbages=%d\n", cld, holder, _garbages->size());
        CLDHandleClosure<RemarkHandle> remarker;
        cld->oops_do(&remarker, ClassLoaderData::_claim_none);
        postcond(rtHeap::is_alive(holder));
        postcond(cld->class_loader() == NULL || rtHeap::is_alive(cld->class_loader()));
      } else {
        rtgc_log(LOG_OPT(2), "destroyed cld handles %p/%p\n", cld, holder);
        postcond(!rtHeap::is_alive(holder));
        postcond(cld->class_loader() == NULL || !rtHeap::is_alive(cld->class_loader()));
      }
    }

    bool is_alive(oop obj) {
      GCObject* node = to_obj(obj);
      if (node->isTrackable()) {
        return !_rtgc.g_pGarbageProcessor->detectGarbage(node);
      } else {
        return obj->is_gc_marked();
      }
    }

    bool has_any_alive_klass(ClassLoaderData* cld, oop holder) {
      oop cl = cld->class_loader();
      precond(cl != NULL);
      _is_alive = cl != NULL && is_alive(cl);
      if (!_is_alive) {
        cld->classes_do(this);
      }
      return _is_alive;
    }

    void do_klass(Klass* k) {
      debug_only(_cnt_handle ++;)
      if (_is_alive) return;
      oop mirror = k->java_mirror_no_keepalive();
      _is_alive = is_alive(mirror);
    }
  };

  static WeakCLDScanner<true>  weak_cld_remarker;
  static WeakCLDScanner<false> weak_cld_cleaner;  
}

ClassLoaderData* rtCLDCleaner::tenured_class_loader_data(oop obj, bool ignoreMirrorClass) {
  Klass* klass = obj->klass();
  switch (klass->id()) {
    case InstanceKlassID:
    case InstanceRefKlassID:
    case ObjArrayKlassID:
      break;
      
    case InstanceClassLoaderKlassID:
      if (ignoreMirrorClass) return NULL;
      return java_lang_ClassLoader::loader_data_raw(obj);

    case InstanceMirrorKlassID:
      if (!ignoreMirrorClass) {
        klass = java_lang_Class::as_Klass_raw(obj);
        if (klass != NULL) break;
      }
    default:
      return NULL;
  }
  return klass->class_loader_data();
}

void rtCLDCleaner::lock_cld(oopDesc* obj) {
  ClassLoaderData* cld = tenured_class_loader_data(obj);
  if (cld != NULL) cld->increase_holder_ref_count();
}

void rtCLDCleaner::unlock_cld(oopDesc* obj) {
  ClassLoaderData* cld = tenured_class_loader_data(obj);
  if (cld != NULL) {
    cld->decrease_holder_ref_count();
    if (cld->holder_ref_count() == 0) {
      g_need_rescan_cld = true;
    }
  }
}

void rtCLDCleaner::resurrect_cld(oopDesc* obj) {
  ClassLoaderData* cld = tenured_class_loader_data(obj, false);
  if (cld == NULL || cld->holder_ref_count() > 0) return;
  oopDesc* holder = cld->holder_no_keepalive();
  precond(holder != NULL);
  if (holder != NULL) {
    if (obj == holder || to_obj(holder)->isGarbageMarked()) {
      CLDHandleClosure<RemarkHandle> resurrector;
      rtgc_log(LOG_OPT(2), "resurrect cld %p/%p rc=%d\n", cld, holder, cld->holder_ref_count());
      cld->oops_do(&resurrector, ClassLoaderData::_claim_none);      
    }
  }
}

void rtCLDCleaner::clear_cld_locks(RtYoungRootClosure* tenuredScanner) {
  precond(rtHeap::in_full_gc);
  debug_only(idx_cld = 0;)
  ClassLoaderDataGraph::roots_cld_do(NULL, &weak_cld_cleaner);
  tenuredScanner->do_complete();
}

void rtCLDCleaner::collect_garbage_clds(RtYoungRootClosure* tenuredScanner) {
  debug_only(idx_cld = 0;)
  ClassLoaderDataGraph::roots_cld_do(NULL, &weak_cld_remarker);
  tenuredScanner->do_complete();

  _rtgc.g_pGarbageProcessor->validateGarbageList();
  // _rtgc.g_pGarbageProcessor->collectGarbage(true);
}

void rtCLDCleaner::initialize() {
  weak_cld_remarker.init();
  weak_cld_cleaner.init();
}