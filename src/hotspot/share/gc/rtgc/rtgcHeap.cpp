#include "precompiled.hpp"

#include "runtime/globals.hpp"
#include "utilities/debug.hpp"
#include "oops/oop.inline.hpp"
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

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

#include "rtRefProcessor.hpp"

bool rtHeap::full_RTGC = true;
bool rtHeap::in_full_gc = false;

using namespace rtHeapUtil;

namespace RTGC {
  extern bool REF_LINK_ENABLED;
  bool ENABLE_GC = true && REF_LINK_ENABLED;
  class RtAdjustPointerClosure: public BasicOopIterateClosure {
  public:
    template <typename T> void do_oop_work(T* p);
    virtual void do_oop(oop* p)       { do_oop_work(p); }
    virtual void do_oop(narrowOop* p) { do_oop_work(p); }
    virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }

    bool is_in_young(void* p) { return p < _old_gen_start; }
    void init(oopDesc* old_anchor_p, oopDesc* new_anchor_p, bool is_java_reference) { 
      _old_anchor_p = old_anchor_p; 
      _new_anchor_p = new_anchor_p; 
      _has_young_ref = false; 
    }
    bool _has_young_ref;
    HeapWord* _old_gen_start;
  private:
    oopDesc* _old_anchor_p;
    oopDesc* _new_anchor_p;
  };

/*  
  class RecyclableGarbageArray : public HugeArray<GCObject*> {
    bool isSorted;
  public:
    RecyclableGarbageArray() {}

    void sort() {
        if (!isSorted) {
          qsort(this->adr_at(0), this->size(), sizeof(GCObject*), compare_obj_size);
          isSorted = true;
        }
    }

    oopDesc* recycle(size_t word_size);

    void markDirtySort() { isSorted = false; }

    void iterate_recycled_oop(DefNewYoungerGenClosure* closure);

    static size_t obj_size(oopDesc* obj) { 
      size_t size = obj->size_given_klass(obj->klass()); 
      return size;
    }

    static int compare_obj_size(const void* left, const void* right);
  };
*/

  
  GrowableArrayCHeap<oop, mtGC> g_pending_trackables;
  GrowableArrayCHeap<oop, mtGC> g_young_roots;
  GrowableArrayCHeap<GCObject*, mtGC> g_stack_roots;
  int g_resurrected_top = INT_MAX;
  Thread* gcThread = NULL;
  int g_cntTrackable = 0;
  int g_cntScan = 0;
  int g_saved_young_root_count = 0;
  RtAdjustPointerClosure g_adjust_pointer_closure;
  BoolObjectClosure* g_young_root_closure;
  const bool USE_PENDING_TRACKABLES = false;
  oopDesc* empty_trackable;
};

using namespace RTGC;


static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

static bool IS_GC_MARKED(oopDesc* obj) {
  return obj->is_gc_marked();
}

static bool is_java_reference(oopDesc* obj, ReferenceType rt) {
  return obj->klass()->id() == InstanceRefKlassID && 
        (rt == (ReferenceType)-1 || ((InstanceRefKlass*)obj->klass())->reference_type() == rt);
}


// static bool is_java_reference(oopDesc* obj) {
//   return (obj->klass()->id() == InstanceRefKlassID);
// }

// static bool is_java_reference_with_young_referent(oopDesc* obj) {
//   if (!is_java_reference(obj)) return false;
//   oop referent = java_lang_ref_Reference::unknown_referent_no_keepalive(obj);
//   return referent != NULL && !to_obj(referent)->isTrackable();
// }


// static void dump_anchor_tree(int depth, GCObject* node) {
//   // for (int i = depth; --i >= 0; ) {
//   //   printf("- ");
//   // }
//   printf("[%d] %p(%s:%d ygR:%d):%d anchors:%d\n", 
//     depth, node, RTGC::getClassName(node), node->isGarbageMarked(), 
//     node->isYoungRoot(), node->getRootRefCount(), node->hasReferrer());
//   if (node->isStrongRootReachable()) return;
//   node->incrementRootRefCount();

//   AnchorIterator it(node);
//   while (it.hasNext()) {
//     GCObject* anchor = it.next();
//     dump_anchor_tree(depth + 1, anchor);
//   }
// }

bool rtHeap::is_alive(oopDesc* p, bool assert_alive) {
  GCObject* node = to_obj(p);
  bool alive = node->isTrackable() ? !node->isGarbageMarked() : p->is_gc_marked();
  if (assert_alive) {
    assert(alive, "invalid pointer %p(%s) isClass=%d\n", 
        p, RTGC::getClassName(to_obj(p)), p->klass() == vmClasses::Class_klass());
  }
  return alive;
}





void rtHeap::add_young_root(oopDesc* old_p, oopDesc* new_p) {
  precond(to_obj(old_p)->isTrackable());
  precond(!to_obj(old_p)->isGarbageMarked());
  to_obj(old_p)->markYoungRoot();
  g_young_roots.append(new_p);
  rtgc_debug_log(old_p, "mark YG Root (%p)->%p idx=%d\n", old_p, new_p, g_young_roots.length());
}

bool rtHeap::is_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isTrackable();
}

void rtHeap::mark_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onAssignRootVariable_internal(obj);
}

void rtHeap::clear_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  GCRuntime::onEraseRootVariable_internal(obj);
}

bool rtHeap::ensure_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isStrongRootReachable();
}

void rtHeap::mark_empty_trackable(oopDesc* p) {
  rtgc_log(LOG_OPT(9), "mark_empty_trackable %p\n", p);
  GCObject* obj = to_obj(p);
  obj->markTrackable();
  debug_only(g_cntTrackable++);
}


/**
 * @brief YG GC 수행 중, old-g로 옮겨진 객체들에 대하여 호출된다.
 */
void rtHeap::mark_promoted_trackable(oopDesc* new_p) {
  // YG GC 수행 중, old-g로 옮겨진 객체들에 대하여 호출된다.
  // 이미 객체가 복사된 상태이다.
  if (to_obj(new_p)->isTrackable()) {
    rtgc_log(LOG_OPT(11), "skip mark_promoted_trackable %p, tr=%d\n", new_p, to_obj(new_p)->isTrackable());
    return;
  }
  to_obj(new_p)->markTrackable();
  // new_p 는 YG 에서 reachable 한 상태일 수 있다. 
  mark_survivor_reachable(new_p);
}


static void resurrect_young_root(GCObject* node) {
  precond(node->isGarbageMarked());
  rtgc_log(LOG_OPT(11), "resurrect obj %p (%s) root=%d\n", 
      node, RTGC::getClassName(node), node->isYoungRoot());
  node->unmarkGarbage();
  if (!g_young_root_closure->do_object_b(cast_to_oop(node))) {
    if (node->isYoungRoot()) {
      node->unmarkYoungRoot();
    }
  } else if (!node->isYoungRoot()) {
    rtHeap::add_young_root(cast_to_oop(node), cast_to_oop(node));
  }
}

void rtHeap__addResurrectedObject(GCObject* node) {
  if (g_resurrected_top == INT_MAX) {
    g_resurrected_top = g_stack_roots.length();
  }
  g_stack_roots.append(node);
}

void rtHeap::keep_alive_trackable(oopDesc* obj) {
  GCObject* node = to_obj(obj);
  precond(node->isTrackable());
  if (node->isGarbageMarked()) {
    resurrect_young_root(node);
  }
}

void rtHeap::mark_survivor_reachable(oopDesc* new_p) {
  GCObject* node = to_obj(new_p);
  // assert(node->isTrackable(), "must be trackable %p(%s)\n", new_p, RTGC::getClassName(to_obj(new_p)));
  if (node->isGarbageMarked()) {
    assert(node->isTrackable(), "no y-root %p(%s)\n",
        node, RTGC::getClassName(node));
    resurrect_young_root(node);
  }

  if (node->isStrongRootReachable()) return;
  rtgc_log(LOG_OPT(9), "add stack root %p\n", new_p);
  GCRuntime::onAssignRootVariable_internal(node);
  g_stack_roots.append(node);
}


/** Reference 가 old-G로 이동한 경우, full-GC 전까지 해당 referent를 reachable 상태로 유지한다. */
void rtHeap::mark_young_root_reachable(oopDesc* p) {
    fatal("deprecated!");
  rtgc_debug_log(p, "mark_young_root_reachable %p\n", p);
  GCObject* node = to_obj(p);
  if (node->isGarbageMarked()) {
    precond(node->isYoungRoot());
    fatal("gotcha!");
    resurrect_young_root(node);
  } else {
    postcond(!node->isGarbageMarked());
    // , "garbage ref-link %p(%s)->%p(%s)\n",
    //   (void*)anchor, anchor->klass()->name()->bytes(),
    //   (void*)obj, obj->klass()->name()->bytes());
  }
}


void rtHeap__clear_garbage_young_roots(bool isTenured) {
  int cnt_root = g_saved_young_root_count;
  if (cnt_root > 0) {
    oop* src_0 = g_young_roots.adr_at(0);
    if (ENABLE_GC) {
      auto garbage_list = _rtgc.g_pGarbageProcessor->getGarbageNodes();
      for (int i = garbage_list->size(); --i >= 0; ) {
        GCObject* yg_root = garbage_list->at(i);
        if (!yg_root->isGarbageMarked()) {
          garbage_list->removeFast(i);
          rtgc_log(LOG_OPT(8), "resurrected yg-root %p\n", yg_root);
        } else {
          rtgc_log(LOG_OPT(8), "collectGarbage yg-root %p\n", yg_root);
        }
      }

      oop* end = src_0 + cnt_root;
      oop* end_of_new_root = src_0 + g_young_roots.length();
      for (; end < end_of_new_root; end++) {
        GCRuntime::onAssignRootVariable_internal(to_obj(*end));
      }

      _rtgc.g_pGarbageProcessor->collectGarbage(isTenured);//reinterpret_cast<GCObject**>(src_0), cnt_root);

      end = src_0 + cnt_root;
      for (; end < end_of_new_root; end++) {
        GCRuntime::onEraseRootVariable_internal(to_obj(*end));
      }

    }
    rtgc_log(LOG_OPT(8), "collectGarbage yg-root finished\n")
    oop* dst = src_0;
    oop* end = src_0 + cnt_root;

    // 1차 검사시 unsafe 상태가 아니었으나, 다른 root 객체를 scan 하는 도중 garbage 로 마킹된 root 객체가 있을 수 있다.
    // 이에 가비지 검색 종료 후, 다시 가비지 여부를 판별하여야 한다.
    for (oop* src = src_0; src < end; src++) {
      GCObject* node = to_obj(*src);
      if (!node->isGarbageMarked() && node->isYoungRoot()) {
        if (dst != src) {
          *dst = cast_to_oop(node);
        }
        dst ++;
      }
    }

    int remain_roots = dst - src_0;
    int new_roots = g_young_roots.length() - cnt_root;
    if (new_roots > 0) {
      int space = cnt_root - remain_roots;
      if (space >= new_roots) {
        memcpy(dst, g_young_roots.adr_at(cnt_root), new_roots * sizeof(void*));
      }
      else if (space > 0) {
        memcpy(dst, g_young_roots.adr_at(g_young_roots.length() - space), space * sizeof(void*));
      }
      remain_roots += new_roots;
    }

    rtgc_log(LOG_OPT(8), "rtHeap__clear_garbage_young_roots done %d->%d\n", 
        g_young_roots.length(), remain_roots);
    g_young_roots.trunc_to(remain_roots);
    g_saved_young_root_count = 0;
  }

#ifdef ASSERT
  if ((cnt_root = g_young_roots.length()) > 0) {
    oop* src = g_young_roots.adr_at(0);
    oop* end = src + cnt_root;
    for (; src < end; src++) {
      GCObject* node = to_obj(*src);
      precond(!ENABLE_GC || !node->isGarbageMarked());
      //rtgc_log(LOG_OPT(8), "YG root %p\n", node); 
    }
  }
#endif
}

void rtHeap__clearStack() {
  int cnt_root = g_stack_roots.length();
  if (cnt_root > 0) {
    GCObject** src = &g_stack_roots.at(0);
    GCObject** end = src + cnt_root;
    rtgc_log(LOG_OPT(8), "clear_stack_roots %d\n", 
        g_stack_roots.length());
    for (; src < end; src++) {
      GCRuntime::onEraseRootVariable_internal(src[0]);
    }
    rtgc_log(LOG_OPT(8), "iterate_stack_roots done %d\n", 
        g_stack_roots.length());
    g_stack_roots.trunc_to(0);
  }
}

void rtHeap::iterate_young_roots(BoolObjectClosure* closure, bool is_full_gc) {
  g_young_root_closure = closure;
  g_saved_young_root_count = g_young_roots.length();
  rtgc_log(LOG_OPT(8), "iterate_young_roots %d stack %d\n", 
      g_saved_young_root_count, g_stack_roots.length());

  if (g_saved_young_root_count == 0) return;

  if (is_full_gc) {
    rtHeap__clear_garbage_young_roots(is_full_gc);
    return;
  }

  oop* src = g_young_roots.adr_at(0);
  oop* end = src + g_saved_young_root_count;

  for (oop* p = src; p < end; p++) {
    GCObject* node = to_obj(*p);
    assert(!node->isGarbageMarked(), "invalid yg-root %p(%s)\n", node, RTGC::getClassName(node));
  }

  for (;src < end; src++) {
    GCObject* node = to_obj(*src);
    // if (is_full_gc) {
      if (_rtgc.g_pGarbageProcessor->detectGarbage(node)) continue;
    // } else if (node->isUn)
  if (is_full_gc) {
    continue;
  }


    // referent 자동 검사됨.
    rtgc_log(LOG_OPT(8), "iterate yg root %p\n", (void*)node);
    bool is_root = closure->do_object_b(cast_to_oop(node));
    if (!is_root) {
      node->unmarkYoungRoot();
    } 
  }
  _rtgc.g_pGarbageProcessor->validateGarbageList();
}


void rtHeap::add_promoted_link(oopDesc* anchor, oopDesc* link, bool is_tenured_link) {
  if (anchor == link) return;
  GCObject* node = to_obj(link);
  rtgc_debug_log(node, "add link %p -> %p\n", anchor, link);
  assert(!to_obj(anchor)->isGarbageMarked(), "grabage anchor %p(%s)\n", anchor, anchor->klass()->name()->bytes());
  if (node->isGarbageMarked()) {
    resurrect_young_root(node);
  }

  precond(to_obj(anchor)->isTrackable() && !to_obj(anchor)->isGarbageMarked());
  RTGC::add_referrer_ex(link, anchor, false);
}

void rtHeap::mark_forwarded(oopDesc* p) {
  // rtgc_log(LOG_OPT(4), "marked %p\n", p);
  precond(!to_node(p)->isGarbageMarked());
  debug_only(precond(!to_node(p)->isUnstableMarked()));
  to_obj(p)->markDirtyReferrerPoints();
}

void rtHeap::mark_pending_trackable(oopDesc* old_p, void* new_p) {
  /**
   * @brief Full-GC 과정에서 adjust_pointers 를 수행하기 직전에 호출된다.
   * 즉, old_p 객체의 field는 유효한 old 객체를 가리키고 있다.
   */
  rtgc_debug_log(old_p, "mark_pending_trackable %p -> %p\n", old_p, new_p);
  precond((void*)old_p->forwardee() == new_p || (old_p->forwardee() == NULL && old_p == new_p));
  to_obj(old_p)->markTrackable();
  debug_only(g_cntTrackable++);
}

template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  if (old_p == NULL || old_p == _old_anchor_p) return;

#ifdef ASSERT
  ensure_alive_or_deadsapce(old_p);
  RTGC::adjust_debug_pointer(old_p, new_p);
#endif   

  _has_young_ref |= is_in_young(new_p);
  if (_new_anchor_p == NULL) return;

  // _old_anchor_p 는 old-address를 가지고 있으므로, Young root로 등록할 수 없다.
  if (to_obj(old_p)->isDirtyReferrerPoints()) {
    // old_p 에 대해 adjust_pointers 를 수행하기 전.
    //RTGC::add_referrer_ex(old_p, _old_anchor_p, false);
    RTGC::add_referrer_unsafe(old_p, _old_anchor_p, _old_anchor_p);
  }
  else {
    // old_p 에 대해 이미 adjust_pointers 가 수행됨.
    RTGC::add_referrer_unsafe(old_p, _new_anchor_p, _old_anchor_p);
  }
}


static bool adjust_anchor_pointer(ShortOOP* p, GCObject* node) {
  GCObject* old_p = p[0];
  if (old_p->isGarbageMarked()) {
    rtgc_log(LOG_OPT(11), "garbage anchor %p in %p\n", old_p, node);
    return false;
  }

  GCObject* new_obj = (GCObject*)cast_to_oop(old_p)->mark().decode_pointer();
  if (new_obj != NULL) {
    precond(new_obj != (void*)0xbaadbabebaadbabc);
    rtgc_log(LOG_OPT(11), "anchor moved %p->%p in %p\n", 
      old_p, new_obj, node);
    p[0] = new_obj;
  }  
  return true;
}

static void __adjust_anchor_pointers(oopDesc* old_p) {
  precond(old_p->is_gc_marked() || 
      (old_p->forwardee() == NULL && !RtLateClearGcMark));

  GCObject* obj = to_obj(old_p);
  precond(!obj->isGarbageMarked());

  const bool CHECK_GARBAGE = RtLateClearGcMark;
  bool check_shortcut;

  if (!obj->hasReferrer()) {
    check_shortcut = true;
  }
  else if (obj->hasMultiRef()) {
    ReferrerList* referrers = obj->getReferrerList();
    ShortOOP* ppAnchor = referrers->adr_at(0);
    int cntAnchor = referrers->size();
    check_shortcut = ppAnchor[0]->isGarbageMarked();
    for (int idx = 0; idx < cntAnchor; ) {
      if (adjust_anchor_pointer(ppAnchor, obj) || !CHECK_GARBAGE) {
        ppAnchor++; idx++;
      } else {
        ppAnchor[0] = referrers->at(--cntAnchor);
      }
    }

    if (CHECK_GARBAGE && cntAnchor < 2) {
      obj->setHasMultiRef(false);
      if (cntAnchor == 0) {
        obj->_refs = 0;
        rtgc_debug_log(obj, "anchor-list cleared %p\n", obj);
      }
      else {
        GCObject* remained = referrers->at(0);
        obj->_refs = _pointer2offset(remained);
      }
      _rtgc.gRefListPool.delete_(referrers);
    }
    else if (cntAnchor != referrers->size()) {
      referrers->resize(cntAnchor);
    }
  }
  else {
    check_shortcut = (!adjust_anchor_pointer((ShortOOP*)&obj->_refs, obj) && CHECK_GARBAGE);
    if (check_shortcut) {
      obj->_refs = 0;
      rtgc_debug_log(obj, "single anchor cleared %p\n", obj);
    }
  }

  if (check_shortcut) {
    if (obj->hasShortcut()) {
      rtgc_log(LOG_OPT(9), "broken shortcut found [%d] %p\n", obj->getShortcutId(), obj);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      SafeShortcut* ss = obj->getShortcut();
      if (ss->tail() != obj) {
        // adjust_point 가 완료되지 않아 validateShortcut() 실행 불가.
        ss->anchor_ref() = obj;
      } else {
        delete ss;
      }
      obj->invalidateSafeAnchor();
    }
  }
}


size_t rtHeap::adjust_pointers(oopDesc* old_p) {
  if (!is_alive(old_p)) {
    precond(!old_p->is_gc_marked() || is_dead_space(old_p));
    int size = old_p->size_given_klass(old_p->klass());
    return size;
  }

#ifdef ASSERT
  g_cntScan ++;
#endif

  oopDesc* new_anchor_p = NULL;
  bool is_java_ref = false;
  if (!to_obj(old_p)->isTrackable()) {
    oopDesc* p = old_p->forwardee();
    if (p == NULL) p = old_p;
    if (!g_adjust_pointer_closure.is_in_young(p)) {
      mark_pending_trackable(old_p, p);
      new_anchor_p = p;
    }
  }

  rtgc_log(LOG_OPT(8), "adjust_pointers %p->%p\n", old_p, new_anchor_p);
  g_adjust_pointer_closure.init(old_p, new_anchor_p, is_java_ref);
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool is_young_root = g_adjust_pointer_closure._has_young_ref && to_obj(old_p)->isTrackable();
  if (is_young_root) {
    oopDesc* forwardee = old_p->forwardee();
    if (forwardee == NULL) forwardee = old_p;
    add_young_root(old_p, forwardee);
  }

  __adjust_anchor_pointers(old_p); 

  to_obj(old_p)->unmarkDirtyReferrerPoints();
  return size; 
}


void rtHeap::prepare_point_adjustment() {
  if (g_adjust_pointer_closure._old_gen_start != NULL) return;

  void* old_gen_heap_start = GenCollectedHeap::heap()->old_gen()->reserved().start();
  g_adjust_pointer_closure._old_gen_start = (HeapWord*)old_gen_heap_start;
  rtgc_log(LOG_OPT(8), "old_gen_heap_start %p\n", old_gen_heap_start);
  if (g_young_roots.length() > 0) {
    oop* src_0 = g_young_roots.adr_at(0);
    oop* dst = src_0;
    oop* end = src_0 + g_young_roots.length();
    for (oop* src = src_0; src < end; src++) {
      GCObject* node = to_obj(*src);
      node->unmarkYoungRoot();
    }
    g_young_roots.trunc_to(0);
  }
}

void GCNode::markGarbage(const char* reason)  {
  if (reason != NULL) {
    rtgc_debug_log(this, "garbage marking on %p(%s) %s\n", this, getClassName(this), reason);
  }
  assert(!this->isGarbageMarked(),
      "already marked garbage %p(%s)\n", this, getClassName(this));
  assert(!cast_to_oop(this)->is_gc_marked() || reason == NULL,
      "invalid garbage marking on %p(%s)\n", this, getClassName(this));
  _flags.isGarbage = true;
  debug_only(unmarkUnstable();)
}

#ifdef ASSERT
static void mark_ghost_anchors(GCObject* node) {
  if (node->isUnreachable()) return;
  precond(node->getRootRefCount() == 0);
  const int discovered_off = java_lang_ref_Reference::discovered_offset();
  AnchorIterator ai(node);
  while (ai.hasNext()) {
    GCObject* anchor = ai.next();
    if (!anchor->isGarbageMarked() && !is_java_reference(cast_to_oop(anchor), (ReferenceType)-1)) {
      if (cast_to_oop(anchor)->is_gc_marked()) {
        cast_to_oop(anchor)->print_on(tty);
      }

      assert(!cast_to_oop(anchor)->is_gc_marked(), "wrong anchor %p(%s) of garbage %p(%s) dp=%p\n", 
          anchor, RTGC::getClassName(anchor), node, RTGC::getClassName(node), 
          !is_java_reference(cast_to_oop(anchor), REF_PHANTOM) ? NULL :
              (void*)(oop)RawAccess<>::oop_load_at(cast_to_oop(anchor), discovered_off));
      if (!anchor->isUnstableMarked()) {
        rtgc_log(LOG_OPT(4), "mark ghost anchor %p(%s)\n", anchor, "");//RTGC::getClassName(anchor))
        anchor->markUnstable();
      }
    }
  }
}
#endif


void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
  precond(node->isGarbageMarked());
  if (node->isGarbageMarked()) return;
  rtgc_debug_log(p, "destroyed %p(ss)\n", node);//, RTGC::getClassName(node));
  rtgc_log(LOG_OPT(4), "destroyed %p(ss)\n", node);//, RTGC::getClassName(node));
  
#ifdef ASSERT
  if (ENABLE_GC) {  
    assert(node->getRootRefCount() == 0, "wrong refCount(%x) on garbage %p(%s)\n", 
        node->getRootRefCount(), node, RTGC::getClassName(node));
    // mark_ghost_anchors(node);
  }
#endif

  rtgc_log(LOG_OPT(11), "trackable destroyed %p, yg-r=%d\n", node, node->isYoungRoot());

  node->markGarbage("destroy_trackable");
  node->removeAnchorList();

  rtgc_log(LOG_OPT(4), "destroyed done %p(%s)\n", node, RTGC::getClassName(node));

  // rtgc_log(LOG_OPT(4), "destroyed done %p\n", node);
  if (!node->isTrackable()) {
    precond(!node->hasShortcut());
    return;
  }

  if (node->hasShortcut()) {
    SafeShortcut* ss = node->getShortcut();
    if (ss->tail() == node) {
      // rtgc_log(true, "garbage shortcut found [%d] %p\n", s_id, node);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      delete ss;
    }
  }
}

void rtHeap::finish_adjust_pointers(bool is_full_gc) {
  if (is_full_gc) {
    g_adjust_pointer_closure._old_gen_start = NULL;
    GCRuntime::adjustShortcutPoints();
  } else {
    rtHeap__clear_garbage_young_roots(is_full_gc);
    rtHeap__clearStack();
  }
  rtHeapEx::adjust_ref_q_pointers(is_full_gc);
}

void rtHeap::prepare_rtgc(bool is_full_gc) {
  if (is_full_gc) {
    rtHeapEx::validate_trackable_refs();
    FreeMemStore::clearStore();
    in_full_gc = true;
  }
}

void rtHeap::finish_rtgc() {
  in_full_gc = false;
  rtHeap__clearStack();
}


void rtHeap::lock_jni_handle(oopDesc* p) {
  if (!REF_LINK_ENABLED) return;
  rtgc_debug_log(p, "lock_handle %p\n", p);
  GCRuntime::onAssignRootVariable_internal(to_obj(p));
}

void rtHeap::release_jni_handle(oopDesc* p) {
  if (!REF_LINK_ENABLED) return;
  assert(to_obj(p)->isStrongRootReachable(), "wrong handle %p\n", p);
  rtgc_debug_log(p, "release_handle %p\n", p);
  GCRuntime::onEraseRootVariable_internal(to_obj(p));
}


void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.length(), full_gc); 
}

// int RecyclableGarbageArray::compare_obj_size(const void* left, const void* right) {
//   int left_size = obj_size(cast_to_oop(*(void**)left));
//   int right_size = obj_size(cast_to_oop(*(void**)right));
//   return left_size - right_size;
// }

void rtHeap::oop_recycled_iterate(DefNewYoungerGenClosure* closure) {
  for (int idx = g_resurrected_top; idx < g_stack_roots.length(); idx++) {
    GCObject* node = g_stack_roots.at(idx);
    if (!node->isTrackable()) {
      GCRuntime::onAssignRootVariable_internal(node);
      closure->do_iterate(cast_to_oop(node));
    }
    g_resurrected_top = INT_MAX; 
  }
}


void __check_old_p(oopDesc* old, oopDesc* new_p) {
  GCObject* node = to_obj(old);
  node->clear_copyed_old_obj();
  // GCObject* obj = to_obj(new_p);

  // if (node->getRootRefCount() > 0
  // ||  node->hasReferrer()
  // ||  node->getShortcutId() != NO_SAFE_ANCHOR) {
  //   precond(node->getRootRefCount() == obj->getRootRefCount());
  //   int anchors = node->hasMultiRef() ? 2 : node->hasReferrer() ? 1 : 0;
  //   rtgc_log(true, "%p rc=%d, anchor=%d shorcut=%d\n",
  //     node, node->getRootRefCount(), anchors, node->getShortcutId());
  // }
}

