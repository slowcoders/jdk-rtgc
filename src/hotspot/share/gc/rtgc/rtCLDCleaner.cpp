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
  static const int CLD_ALIVE    = 0;
  static const int CLD_DIRTY    = 1;
  static const int CLD_RELEASED = 2;
  static const int CLD_GARBAGE  = 3;

  debug_only(static int idx_cld = 0;)

  enum CldHandleAction {
    ClearHandle,
    RemarkHandle,
    MarkUntrackable,
  };

  static ClassLoaderData* g_dirty_cld_q = NULL;
  static inline ClassLoaderData* tenured_class_loader_data(oop obj, bool checkInstance, bool checkMirrorClass) {
    Klass* klass = obj->klass();
    switch (klass->id()) {
      case InstanceKlassID:
      case InstanceRefKlassID:
      case ObjArrayKlassID:
        if (!checkInstance) return NULL;
        break;
        
      case InstanceClassLoaderKlassID:
        if (!checkMirrorClass) return NULL;
        return java_lang_ClassLoader::loader_data_raw(obj);

      case InstanceMirrorKlassID:
        if (!checkMirrorClass) return NULL;
        klass = java_lang_Class::as_Klass_raw(obj);
        if (klass == NULL) return NULL;
        break;

      default:
        return NULL;
    }
    return klass->class_loader_data();    
  }

  template<CldHandleAction action>
  class CLDHandleClosure : public OopClosure {
  public:
    ClassLoaderData* _cld;

    CLDHandleClosure(ClassLoaderData* cld) {
      _cld = cld;
    }

    virtual void do_oop(oop* o) { do_oop_work(o); }
    virtual void do_oop(narrowOop* o) { fatal("cld handle not compressed"); }

    void do_oop_work(oop* o) {
      oop obj = *o;
      debug_only(_cld ++;)
      if (obj != NULL) {
        GCObject* node = to_obj(obj);
        if (action == ClearHandle) {
          assert(!node->isTrackable() || rtHeap::is_alive(obj), 
              "invalid handle %p(%s) rc=%d\n", 
              node, RTGC::getClassName(node), node->getRootRefCount());
          rtHeap::release_jni_handle(obj);

          if (node->getRootRefCount() == 2) {
            ClassLoaderData* cld = tenured_class_loader_data(obj, false, true);
            if (cld != NULL && obj == cld->holder_no_keepalive()) {
              precond(cld->holder_state() == CLD_ALIVE);
              rtgc_log(LOG_OPT(2), "dirty cld holder %p/%p\n", cld, node);
              cld->set_holder_state(CLD_DIRTY);
              g_dirty_cld_q = cld;
            }
          }
          return;
        }

        if (action == RemarkHandle) {
          if (node->isGarbageMarked()) {
            // CLD 가 계층적으로 참조된 경우, 하위 CLD 에 대한 detectGarbage 에 의해
            // 상위 handle이 garbageMarking 된 상태가 될 수 있다. 
            assert(node->isTrackable(), "invalid handle %p(%s) cld=[%p] idx=%d rc=%d\n", 
                node, RTGC::getClassName(node), _cld, idx_cld, node->getReferrerCount());
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

  template<int cld_state>
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
        const bool mark_ref = cld_state == CLD_RELEASED;
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
        CLDHandleClosure<mark_ref ? RemarkHandle : ClearHandle> closure(cld);
        cld->oops_do(&closure, ClassLoaderData::_claim_none);
        return;
      }

      if (cld_state != cld->holder_state()) return;

      oopDesc* holder = cld->holder_no_keepalive();
      assert(holder != NULL, "WeakCLDScanner must be called befor weak-handle cleaning.");
      
      if (cld_state != CLD_RELEASED) {
        if (cld->holder_ref_count() > 0 || cld->class_loader() == NULL ||
          (to_obj(holder)->isTrackable() ? to_obj(holder)->getRootRefCount() > 2 : holder->is_gc_marked())) {
          rtgc_log(false && LOG_OPT(2), "skip cld scanner %p/%p(%s) cld_rc=%d, rc=%d tr=%d\n", 
              cld, holder, RTGC::getClassName(holder), cld->holder_ref_count(), 
              to_obj(holder)->getRootRefCount(), to_obj(holder)->isTrackable());
          CLDHandleClosure<MarkUntrackable> marker(cld);
          cld->oops_do(&marker, ClassLoaderData::_claim_none);
        } else {
          rtgc_log(LOG_OPT(2), "cleaning cld %p/%p cld_rc=%d, rc=%d tr=%d\n", 
              cld, holder, cld->holder_ref_count(), to_obj(holder)->getRootRefCount(), to_obj(holder)->isTrackable());
          CLDHandleClosure<ClearHandle> cleaner(cld);
          cld->oops_do(&cleaner, ClassLoaderData::_claim_none);
          cld->set_holder_state(CLD_RELEASED);
        }
        return;
      }

      int top = _garbages->size();
      if (has_any_alive_klass(cld, holder)) {
        cld->set_holder_state(CLD_ALIVE);
        rtgc_log(LOG_OPT(2), "remark cld handles %p/%p garbages=%d\n", cld, holder, _garbages->size());
        CLDHandleClosure<RemarkHandle> remarker(cld);
        cld->oops_do(&remarker, ClassLoaderData::_claim_none);
        postcond(rtHeap::is_alive(holder));
        postcond(cld->class_loader() == NULL || rtHeap::is_alive(cld->class_loader()));
      } else {
        cld->set_holder_state(CLD_GARBAGE);
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

  static WeakCLDScanner<CLD_ALIVE>    release_cld_holder;  
  static WeakCLDScanner<CLD_DIRTY>    release_dirty_cld_holder;  
  static WeakCLDScanner<CLD_RELEASED> unload_cld_holder;
}


void rtCLDCleaner::lock_cld(oopDesc* obj) {
  ClassLoaderData* cld = tenured_class_loader_data(obj, true, false);
  if (cld != NULL) cld->increase_holder_ref_count();
}

void rtCLDCleaner::unlock_cld(oopDesc* obj) {
  ClassLoaderData* cld = tenured_class_loader_data(obj, true, false);
  if (cld != NULL) {
    cld->decrease_holder_ref_count();
    if (cld->holder_ref_count() == 0) {
      // g_need_rescan_cld = true;
    }
  }
}

void rtCLDCleaner::resurrect_cld(oopDesc* obj) {
  ClassLoaderData* cld = tenured_class_loader_data(obj, true, true);
  if (cld == NULL || cld->holder_ref_count() > 0) return;
  oopDesc* holder = cld->holder_no_keepalive();
  precond(holder != NULL);
  if (holder != NULL) {
    if (obj == holder || to_obj(holder)->isGarbageMarked()) {
      CLDHandleClosure<RemarkHandle> resurrector(cld);
      rtgc_log(LOG_OPT(2), "resurrect cld %p/%p rc=%d\n", cld, holder, cld->holder_ref_count());
      cld->oops_do(&resurrector, ClassLoaderData::_claim_none);      
    }
  }
}

void rtCLDCleaner::clear_cld_locks(RtYoungRootClosure* tenuredScanner) {
  precond(rtHeap::in_full_gc);
  debug_only(idx_cld = 0;)
  ClassLoaderDataGraph::roots_cld_do(NULL, &release_cld_holder);
  tenuredScanner->do_complete();
}

void rtCLDCleaner::collect_garbage_clds(RtYoungRootClosure* tenuredScanner) {
  debug_only(idx_cld = 0;)
  ClassLoaderDataGraph::roots_cld_do(NULL, &unload_cld_holder);
  while (g_dirty_cld_q != NULL) {
    g_dirty_cld_q = NULL;
    ClassLoaderDataGraph::roots_cld_do(NULL, &release_dirty_cld_holder);
    ClassLoaderDataGraph::roots_cld_do(NULL, &unload_cld_holder);
  }

  tenuredScanner->do_complete();
  _rtgc.g_pGarbageProcessor->validateGarbageList();
}

void rtCLDCleaner::initialize() {
  unload_cld_holder.init();
  release_cld_holder.init();
}