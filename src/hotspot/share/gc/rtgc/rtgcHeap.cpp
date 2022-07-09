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

  class RecyclableGarbageArray : public HugeArray<GCObject*> {
    bool isSorted;
    int recycled_count;
  public:
    RecyclableGarbageArray() : recycled_count(0) {}

    void sort() {
        if (!isSorted) {
          qsort(this->adr_at(0), this->size(), sizeof(GCObject*), compare_obj_size);
          isSorted = true;
        }
    }

    oopDesc* recycle(size_t word_size);

    void markDirtySort() { isSorted = false; }

    void reset_recycled_count() { 
      if (false && recycled_count > 0) {
        GCObject** p = this->adr_at(this->size() - recycled_count);
        GCObject** end = p + recycled_count;
        for (;p < end; p++) {
          GCObject* node = *p;
          GCRuntime::onEraseRootVariable_internal(node);
        }
        this->resize(this->size() - recycled_count); 
        recycled_count = 0; 
      }
    }

    void iterate_recycled_oop(DefNewYoungerGenClosure* closure);

    static size_t obj_size(oopDesc* obj) { 
      size_t size = obj->size_given_klass(obj->klass()); 
      return size;
    }

    static int compare_obj_size(const void* left, const void* right);
  };

  class RtRefProcessor {
  public:
    oopDesc* _pending_head;
    oopDesc* _phantom_ref_q;
    oopDesc* _enqueued_top;
#ifdef ASSERT
    int cnt_garbage;
    int cnt_phantom;
    int cnt_pending;
    int cnt_cleared;
    int cnt_alive;
#endif
  
    RtRefProcessor() { _phantom_ref_q = NULL; }

    template <bool is_full_gc>
    void process_phantom_references();
    void register_pending_refereneces();
  };

  
  GrowableArrayCHeap<oop, mtGC> g_pending_trackables;
  GrowableArrayCHeap<oop, mtGC> g_young_roots;
  int g_cntGarbageYGRoot = 0;
  GrowableArrayCHeap<GCObject*, mtGC> g_stack_roots;
  int g_resurrected_top = INT_MAX;
  RecyclableGarbageArray g_garbage_list;
  Thread* gcThread = NULL;
  int g_cntTrackable = 0;
  int g_cntScan = 0;
  int g_saved_young_root_count = 0;
  RtRefProcessor g_rtRefProcessor;
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
//   if (node->getRootRefCount() > 0) return;
//   node->incrementRootRefCount();

//   AnchorIterator it(node);
//   while (it.hasNext()) {
//     GCObject* anchor = it.next();
//     dump_anchor_tree(depth + 1, anchor);
//   }
// }


bool rtHeap::is_alive(oopDesc* p, bool assert_alive) {
  if (assert_alive) {
    assert(!to_obj(p)->isGarbageMarked(), "invalid pointer %p(%s) isClass=%d\n", 
        p, RTGC::getClassName(to_obj(p)), p->klass() == vmClasses::Class_klass());
  }
  return !to_obj(p)->isGarbageMarked();
}



static bool is_dead_space(oopDesc* obj) {
  Klass* klass = obj->klass();
  return klass->is_typeArray_klass() || klass == vmClasses::Object_klass();
}

static void ensure_alive_or_deadsapce(oopDesc* old_p) {
  assert(!to_obj(old_p)->isGarbageMarked() || is_dead_space(old_p), 
        "invalid pointer %p(%s) isClass=%d\n", 
        old_p, RTGC::getClassName(to_obj(old_p)), old_p->klass() == vmClasses::Class_klass());
}



void rtHeap::add_young_root(oopDesc* old_p, oopDesc* new_p) {
  precond(to_obj(old_p)->isTrackable());
  precond(!to_obj(old_p)->isGarbageMarked());
  to_obj(old_p)->markYoungRoot();
  g_young_roots.append(new_p);
  rtgc_log(LOG_OPT(11), "mark YG Root (%p)->%p idx=%d\n", old_p, new_p, g_young_roots.length());
}

bool rtHeap::is_trackable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  return obj->isTrackable();
}

void rtHeap::mark_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  obj->incrementRootRefCount();
}

void rtHeap::clear_weak_reachable(oopDesc* p) {
  GCObject* obj = to_obj(p);
  // rtgc_log(true, "clear_weak_reachable %p\n", p);
  GCRuntime::onEraseRootVariable_internal(obj);
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
    rtgc_log(true || LOG_OPT(11), "skip mark_promoted_trackable %p, tr=%d\n", new_p, to_obj(new_p)->isTrackable());
    return;
  }
  to_obj(new_p)->markTrackable();
  // new_p 는 YG 에서 reachable 한 상태일 수 있다. 
  mark_survivor_reachable(new_p);
}

static void resurrect_young_root(GCObject* node) {
  precond(node->isGarbageMarked());
  //rtgc_log(LOG_OPT(11), "young_root(%p) resurrected\n", node);
  node->unmarkGarbage();
  if (!g_young_root_closure->do_object_b(cast_to_oop(node))) {
    node->unmarkYoungRoot();
  }
}

template <bool is_full_gc>
static oopDesc* get_valid_forwardee(oopDesc* obj) {
  if (is_full_gc) {
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


void rtHeap::mark_survivor_reachable(oopDesc* new_p, bool unused) {
  GCObject* node = to_obj(new_p);
  // assert(node->isTrackable(), "must be trackable %p(%s)\n", new_p, RTGC::getClassName(to_obj(new_p)));
  if (node->isGarbageMarked()) {
    if (!node->isYoungRoot() && new_p->klass() == vmClasses::String_klass()) {
      ResourceMark rm;
      char* ex_msg = java_lang_String::as_utf8_string(new_p);
      rtgc_log(true, "invalid y-root str %p(%s) %d\n", node, ex_msg, java_lang_String::length(new_p));
    }
    assert(node->isYoungRoot(), "no y-root %p(%s)\n",
        node, RTGC::getClassName(node));
    resurrect_young_root(node);
  }

  if (node->getRootRefCount() > 0) return;
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



void rtHeap__clear_garbage_young_roots() {
  int cnt_root = g_saved_young_root_count;
  if (cnt_root > 0) {
    oop* src_0 = g_young_roots.adr_at(0);
    //rtgc_log(true, "collectGarbage yg-root started\n")
    if (ENABLE_GC) {
      oop* end = src_0 + cnt_root;
      int cntGarbage = g_garbage_list.size();
      for (; --g_cntGarbageYGRoot >= 0; ) {
        end --;
        GCObject* node = to_obj(*end);
        if (node->isGarbageMarked()) {
          precond(!node->isAnchored());
          // precond(!g_garbage_list.contains(node));
          g_garbage_list.push_back(node);
        }
      }
      end = src_0 + cnt_root;
      oop* end_of_new_root = src_0 + g_young_roots.length();
      for (; end < end_of_new_root; end++) {
        to_obj(*end)->incrementRootRefCount();
      }


      GarbageProcessor::collectGarbage(reinterpret_cast<GCObject**>(src_0), cnt_root, g_garbage_list, cntGarbage);
      g_garbage_list.markDirtySort();

      end = src_0 + cnt_root;
      for (; end < end_of_new_root; end++) {
        GCRuntime::onEraseRootVariable_internal(to_obj(*end));
      }

    }
    //rtgc_log(true, "collectGarbage yg-root fin  ished\n")
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

    rtgc_log(LOG_OPT(8), "rtHeap__clear_garbage_young_roots done %d->%d garbage=%d\n", 
        g_young_roots.length(), remain_roots, g_garbage_list.size());
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

  if ((cnt_root = g_stack_roots.length()) > 0) {
    GCObject** src = &g_stack_roots.at(0);
    GCObject** end = src + cnt_root;
    for (; src < end; src++) {
      GCRuntime::onEraseRootVariable_internal(src[0]);
    }
    rtgc_log(LOG_OPT(8), "iterate_stack_roots done %d\n", 
        g_stack_roots.length());
    g_stack_roots.trunc_to(0);
  }
}

void rtHeap::iterate_young_roots(BoolObjectClosure* closure, OopClosure* unused) {
  g_young_root_closure = closure;
  g_saved_young_root_count = g_young_roots.length();
  rtgc_log(LOG_OPT(8), "iterate_young_roots %d stack %d\n", 
      g_saved_young_root_count, g_stack_roots.length());

  if (g_saved_young_root_count == 0) return;

  oop* src = g_young_roots.adr_at(0);
  oop* end = src + g_young_roots.length();
  g_cntGarbageYGRoot = 0;
  for (;src < end;) {
    GCObject* node = to_obj(*src);
    assert(!node->isGarbageMarked(), "invalid yg-root %p(%s)\n", node, RTGC::getClassName(node));
    if (ENABLE_GC && !node->isAnchored()) {
      node->markGarbage("not anchored young root");
      end --;
      oop tmp = end[0];
      end[0] = cast_to_oop(node);
      src[0] = tmp;
      g_cntGarbageYGRoot ++;
      rtgc_log(LOG_OPT(2), "skip garbage node %p\n", (void*)node);
      continue;
    }

    // referent 자동 검사됨.
    bool is_root = closure->do_object_b(cast_to_oop(node));
    rtgc_log(LOG_OPT(8), "iterate yg root %p:%d\n", (void*)node, is_root);
    if (!is_root) {
      node->unmarkYoungRoot();
    } else {
      // postcond(!is_java_reference_with_young_referent(cast_to_oop(node))
      //   || !GenCollectedHeap::is_in_young(cast_to_oop(node)));
    }
    src ++;
  }
}


void rtHeap::add_promoted_link(oopDesc* anchor, oopDesc* link, bool is_tenured_link) {
  if (anchor == link) return;
  GCObject* node = to_obj(link);
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
  debug_only(precond(!to_node(p)->isUnstable()));
  if (!USE_PENDING_TRACKABLES) {
    to_obj(p)->markDirtyReferrerPoints();
  }
}

void rtHeap::mark_pending_trackable(oopDesc* old_p, void* new_p) {
  /**
   * @brief Full-GC 과정에서 adjust_pointers 를 수행하기 직전에 호출된다.
   * 즉, old_p 객체의 field는 유효한 old 객체를 가리키고 있다.
   */
  rtgc_log(LOG_OPT(9), "mark_pending_trackable %p (move to -> %p)\n", old_p, new_p);
  precond((void*)old_p->forwardee() == new_p || (old_p->forwardee() == NULL && old_p == new_p));
  to_obj(old_p)->markTrackable();
  debug_only(g_cntTrackable++);
  if (USE_PENDING_TRACKABLES) {
    g_pending_trackables.append((oopDesc*)new_p);
  }
  // if (is_java_reference_with_young_referent(old_p)) {
  //   /* adjust_pointers 수행 전에 referent 검사하여야 한다.
  //      또는 객체 복사가 모두 종료된 시점에 referent를 검사할 수 있다.
  //   */
  //   add_young_root(old_p, (oopDesc*)new_p);
  // }
}

template <typename T>
void RtAdjustPointerClosure::do_oop_work(T* p) { 
  oop new_p;
  oopDesc* old_p = MarkSweep::adjust_pointer(p, &new_p); 
  // rtgc_log(is_java_reference(_old_anchor_p, REF_PHANTOM), 
  //     "reference(%p)  discovered(%p) moved -> %p:%ld/%d\n", _old_anchor_p, (void*)old_p, (void*)new_p,
  //         ((address)p - (address)_old_anchor_p), java_lang_ref_Reference::discovered_offset());
  if (old_p == NULL || old_p == _old_anchor_p) return;

#ifdef ASSERT
  ensure_alive_or_deadsapce(old_p);
  RTGC::adjust_debug_pointer(old_p, new_p);
#endif   

  _has_young_ref |= is_in_young(new_p);
  if (_new_anchor_p == NULL) return;
  if (USE_PENDING_TRACKABLES) return;

  // _old_anchor_p 는 old-address를 가지고 있으므로, Young root로 등록할 수 없다.
  if (to_obj(old_p)->isDirtyReferrerPoints()) {
    // old_p 에 대해 adjust_pointers 를 수행하기 전.
    //RTGC::add_referrer_ex(old_p, _old_anchor_p, false);
    RTGC::add_referrer_unsafe(old_p, _old_anchor_p);
  }
  else {
    // old_p 에 대해 이미 adjust_pointers 가 수행됨.
    RTGC::add_referrer_unsafe(old_p, _new_anchor_p);
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
    }
  }

  if (check_shortcut) {
    int s_id = obj->getShortcutId();
    if (s_id > INVALID_SHORTCUT) {
      rtgc_log(LOG_OPT(9), "broken shortcut found [%d] %p\n", s_id, obj);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      SafeShortcut* ss = obj->getShortcut();
      if (ss->tail() != obj) {
        // adjust_point 가 완료되지 않아 validateShortcut() 실행 불가.
        ss->anchor_ref() = obj;
      } else {
        delete ss;
      }
      obj->setShortcutId_unsafe(obj->hasReferrer() ? INVALID_SHORTCUT : 0);
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

#ifdef ASSERT    
  if (RTGC::is_debug_pointer(old_p) && !to_obj(old_p)->hasReferrer()) {
  // rtgc_debug_log(old_p, 
  //     "adjust_pointers %p->%p(%d)\n", (void*)old_p, get_valid_forwardee<true>(old_p), to_obj(old_p)->isAnchored());
    rtgc_log(true, "adjust_pointers %p->%p(%d)\n", (void*)old_p, get_valid_forwardee<true>(old_p), to_obj(old_p)->isAnchored());
  }
#endif


  g_adjust_pointer_closure.init(old_p, new_anchor_p, is_java_ref);
  size_t size = old_p->oop_iterate_size(&g_adjust_pointer_closure);

  bool is_young_root = g_adjust_pointer_closure._has_young_ref && to_obj(old_p)->isTrackable();
  if (is_young_root) {
    oopDesc* forwardee = old_p->forwardee();
    if (forwardee == NULL) forwardee = old_p;
    add_young_root(old_p, forwardee);
  }

  __adjust_anchor_pointers(old_p); 

  if (!USE_PENDING_TRACKABLES) {
    to_obj(old_p)->unmarkDirtyReferrerPoints();
  }
  return size; 
}


void rtHeap::prepare_point_adjustment() {
  if (g_adjust_pointer_closure._old_gen_start != NULL) return;

  void* old_gen_heap_start = GenCollectedHeap::heap()->old_gen()->reserved().start();
  g_adjust_pointer_closure._old_gen_start = (HeapWord*)old_gen_heap_start;
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
  assert(!cast_to_oop(this)->is_gc_marked(),
      "invalid garbage marking on %p(%s)\n", this, getClassName(this));
  _flags.isGarbage = true;
  debug_only(unmarkUnstable();)
}

#ifdef ASSERT
static void mark_ghost_anchors(GCObject* node) {
  if (!node->isAnchored()) return;
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
      if (!anchor->isUnstable()) {
        rtgc_log(LOG_OPT(4), "mark ghost anchor %p(%s)\n", anchor, "");//RTGC::getClassName(anchor))
        anchor->markUnstable();
        // stack-overflow
        // mark_ghost_anchors(anchor);
      }
    }
  }
}
#endif


void rtHeap::destroy_trackable(oopDesc* p) {
  GCObject* node = to_obj(p);
  if (node->isGarbageMarked()) return;
  rtgc_debug_log(p, "destroyed %p(ss)\n", node);//, RTGC::getClassName(node));
  rtgc_log(LOG_OPT(4), "destroyed %p(ss)\n", node);//, RTGC::getClassName(node));
  
  if (ENABLE_GC) {  
    assert(node->getRootRefCount() == 0, "wrong refCount(%x) on garbage %p(%s)\n", 
        node->getRootRefCount(), node, RTGC::getClassName(node));
  #ifdef ASSERT
    mark_ghost_anchors(node);
  #endif
  }
  rtgc_log(LOG_OPT(11), "trackable destroyed %p, yg-r=%d\n", node, node->isYoungRoot());

  node->markGarbage("destroy_trackable");
  node->removeAnchorList();

  rtgc_log(LOG_OPT(4), "destroyed done %p(%s)\n", node, RTGC::getClassName(node));

  // rtgc_log(LOG_OPT(4), "destroyed done %p\n", node);
  if (!node->isTrackable()) {
    precond(node->getShortcutId() == 0);
    return;
  }

  int s_id = node->getShortcutId();
  if (s_id > INVALID_SHORTCUT) {
    SafeShortcut* ss = node->getShortcut();
    if (ss->tail() == node) {
      // rtgc_log(true, "garbage shortcut found [%d] %p\n", s_id, node);
      // node 가 가비지면 생존경로가 존재하지 않는다.
      delete ss;
    }
  }

}

template <bool mark>
class WeakReachableScanClosure: public BasicOopIterateClosure {
 public:
  virtual void do_oop(oop* p) { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p); }
  virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }
  template <typename T> void do_oop_work(T* p) {
    T heap_oop = RawAccess<>::oop_load(p);
    oop obj = CompressedOops::decode_not_null(heap_oop);
    rtHeap::clear_weak_reachable(obj);
  }
};

void rtHeap::prepare_full_gc() {
  // GenMarkSweep::mark_sweep_phase1() 에서 clear_not_alive 를 통해 처리
  // WeakReachableScanClosure<false> scan_closure;
  // WeakProcessor::oops_do(&scan_closure);
}

void rtHeap::discover_java_references(bool is_tenure_gc) {
  if (is_tenure_gc) {
    g_garbage_list.resize(0);
    g_rtRefProcessor.process_phantom_references<true>();
  } else {
    g_rtRefProcessor.process_phantom_references<false>();
    rtHeap__clear_garbage_young_roots();
    g_garbage_list.reset_recycled_count();
  }

  g_adjust_pointer_closure._old_gen_start = NULL;

  if (USE_PENDING_TRACKABLES) {
    const int count = g_pending_trackables.length();
    rtgc_log(LOG_OPT(11), "finish_collection %d\n", count);
    if (count == 0) return;

    oop* pOop = &g_pending_trackables.at(0);
    for (int i = count; --i >= 0; ) {
      oopDesc* p = *pOop++;
      RTGC::iterateReferents(to_obj(p), (RefTracer2)RTGC::add_referrer_ex, p);
    }
    g_pending_trackables.trunc_to(0);
  }
  return;
}

void rtHeap::release_jni_handle(oopDesc* p) {
  if (!REF_LINK_ENABLED) return;
  assert(to_obj(p)->getRootRefCount() > 0, "wrong handle %p\n", p);
  rtgc_log(p == RTGC::debug_obj, "release_handle %p\n", p);
  GCRuntime::onEraseRootVariable_internal(to_obj(p));
}

void rtHeap::init_java_reference(oopDesc* ref_oop, oopDesc* referent) {
  precond(RtNoDiscoverPhantom);
  precond(referent != NULL);

  ptrdiff_t referent_offset = java_lang_ref_Reference::referent_offset();  
  if (!java_lang_ref_Reference::is_phantom(ref_oop)) {
    HeapAccess<>::oop_store_at(ref_oop, referent_offset, referent);
    rtgc_log(false && ((InstanceRefKlass*)ref_oop->klass())->reference_type() == REF_WEAK, 
          "weak ref %p for %p\n", (void*)ref_oop, referent);
    return;
  }

  //rtgc_log(LOG_OPT(3), "created phantom %p for %p\n", (void*)ref_oop, referent);
  HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(ref_oop, referent_offset, referent);
  oop next_discovered = Atomic::xchg(&g_rtRefProcessor._phantom_ref_q, ref_oop);
  java_lang_ref_Reference::set_discovered_raw(ref_oop, next_discovered);
  return;
}

void rtHeap::link_discovered_pending_reference(oopDesc* ref_q, oopDesc* end) {
  g_rtRefProcessor._enqueued_top = ref_q;
  oopDesc* discovered;
  for (oopDesc* obj = ref_q; obj != end; obj = discovered) {
    discovered = java_lang_ref_Reference::discovered(obj);
    if (to_obj(obj)->isTrackable()) {
      RTGC::add_referrer_ex(discovered, obj, true);
    }
    // rtHeap::link_discovered_pending_reference(obj, discovered);
  }
}

template <bool is_full_gc>
void RtRefProcessor::process_phantom_references() {
  _pending_head = NULL;
#ifdef ASSERT  
  cnt_garbage = 0;
  cnt_phantom = 0;
  cnt_pending = 0;
  cnt_cleared = 0;
  cnt_alive = 0;
#endif

  oop acc_ref = NULL;
  oop next_ref;
  oop alive_head = NULL;
  oopDesc* pending_tail_acc = NULL;
  const int referent_off = java_lang_ref_Reference::referent_offset();
  const int discovered_off = java_lang_ref_Reference::discovered_offset();

  for (oop ref_op = _phantom_ref_q; ref_op != NULL; ref_op = next_ref) {
    rtgc_log(LOG_OPT(3), "check phantom ref %p\n", (void*)ref_op);
    next_ref = RawAccess<>::oop_load_at(ref_op, discovered_off);
    precond(ref_op != next_ref);
    oop ref_np = get_valid_forwardee<is_full_gc>(ref_op);
    if (ref_np == NULL) {
      rtgc_log(LOG_OPT(3), "garbage phantom ref %p removed\n", (void*)ref_op);
      debug_only(cnt_garbage++;)
      continue;
    }

    rtgc_log(LOG_OPT(3), "phantom ref %p moved %p\n", (void*)ref_op, (void*)ref_np);
    oop referent_op = RawAccess<>::oop_load_at(ref_op, referent_off);
    precond(referent_op != NULL);
    acc_ref = is_full_gc ? ref_op : ref_np;
    if (referent_op == ref_op) {
      debug_only(cnt_cleared++;)
      /**
       * Two step referent clear (to hide discoverd-link)
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
      HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(acc_ref, referent_off, oop(NULL));
      rtgc_log(LOG_OPT(3), "reference %p(->%p) with garbage referent linked after (%p)\n", 
            (void*)ref_op, (void*)ref_np, (void*)pending_tail_acc);
      if (_pending_head == NULL) {
        precond(pending_tail_acc == NULL);
        _pending_head = acc_ref;
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

  // if (is_full_gc && alive_head != NULL) {
  //   _phantom_ref_q = get_valid_forwardee<true>(alive_head);
  // } else {
    _phantom_ref_q = alive_head;
  // }
  rtgc_log(LOG_OPT(3), "total phatom scanned %d, garbage %d, cleared %d, pending %d, alive %d q=%p\n",
        cnt_phantom, cnt_garbage, cnt_cleared, cnt_pending, cnt_alive, (void*)alive_head);

  if (_pending_head != NULL) {
    // oopDesc* enqueued_top_np = _enqueued_top;
    // if (is_full_gc && enqueued_top_np != NULL) {
    //   enqueued_top_np = get_valid_forwardee<true>(enqueued_top_np);
    // }
    // precond(enqueued_top_np == NULL || enqueued_top_np == Universe::reference_pending_list());
    oopDesc* enqueued_top_np = Universe::swap_reference_pending_list(_pending_head);
    // assert(enqueued_top_np == _enqueued_top, "np = %p, top=%p\n", enqueued_top_np, _enqueued_top);

    HeapAccess<AS_NO_KEEPALIVE>::oop_store_at(pending_tail_acc, discovered_off, enqueued_top_np);
    if (enqueued_top_np != NULL && to_node(pending_tail_acc)->isTrackable()) {
      RTGC::add_referrer_ex(enqueued_top_np, pending_tail_acc, true);
    }
  }
  _enqueued_top = NULL;
}

bool rtHeap::is_valid_link_of_yg_root(oopDesc* yg_root, oopDesc* link) {
  assert(java_lang_ref_Reference::is_phantom(link)
      && java_lang_ref_Reference::is_phantom(yg_root)
      && (void*)java_lang_ref_Reference::discovered(yg_root) == link
      && (void*)java_lang_ref_Reference::unknown_referent_no_keepalive(yg_root) == NULL
      && to_obj(link)->isYoungRoot(),
      "invalid ref-link %p(%s)->%p(%s) is_yg_root=%d\n",
      (void*)yg_root, yg_root->klass()->name()->bytes(),
      (void*)link, link->klass()->name()->bytes(), 
      to_obj(link)->isYoungRoot());
  return true;
}

void rtHeap::finish_compaction_gc(bool is_full_gc) {
  if (is_full_gc) {
    GCRuntime::adjustShortcutPoints();
  }
}

void rtHeap::print_heap_after_gc(bool full_gc) {  
  rtgc_log(LOG_OPT(1), "trackables = %d, young_roots = %d, full gc = %d\n", 
      g_cntTrackable, g_young_roots.length(), full_gc); 
}

int RecyclableGarbageArray::compare_obj_size(const void* left, const void* right) {
  int left_size = obj_size(cast_to_oop(*(void**)left));
  int right_size = obj_size(cast_to_oop(*(void**)right));
  return left_size - right_size;
}

void rtHeap::oop_recycled_iterate(DefNewYoungerGenClosure* closure) {
  g_garbage_list.iterate_recycled_oop(closure);
}

HeapWord* RtSpace::allocate(size_t word_size) {
  HeapWord* heap = _SUPER::allocate(word_size);
  if (heap == NULL) {
    int cntGarbage = g_garbage_list.size();
    if (cntGarbage > 0) {
      heap = (HeapWord*)g_garbage_list.recycle(word_size);
      RTGC::debug_obj2 = heap;
    }
  }
  else {
    RTGC::debug_obj2 = NULL;
  }
  return heap;
}
HeapWord* RtSpace::par_allocate(size_t word_size) {
  HeapWord* heap = _SUPER::par_allocate(word_size);
  return heap;
}

oopDesc* RecyclableGarbageArray::recycle(size_t word_size) {
  sort();
  int low = 0;
  int high = this->size() - 1;
  int mid;
  GCObject** arr = this->adr_at(0);

  while(low <= high) {
    mid = (low + high) / 2;
    GCObject* node = arr[mid];
    assert(node->isGarbageMarked(), "not a garbage %p\n", node);
    assert(!node->isAnchored(), "garbage must not acnhored %p\n", node);
    size_t size = obj_size(cast_to_oop(node));
    if (size == word_size) {
      if (g_resurrected_top == INT_MAX) {
        g_resurrected_top = g_stack_roots.length();
      }
      this->removeAndShift(mid);
      // assert(!this->contains(node), "item remove fail %ld %p %d(%d)/%d\n", word_size, node, mid, this->indexOf(node), this->size());
      rtgc_log(LOG_OPT(6), "recycle garbage %ld %p %d/%d\n", word_size, node, mid, this->size());
      g_stack_roots.append(node);
      return cast_to_oop(node);
    } else if (size > word_size) {
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  }
  return NULL;
}

void RecyclableGarbageArray::iterate_recycled_oop(DefNewYoungerGenClosure* closure) {
  for (int idx = g_resurrected_top; idx < g_stack_roots.length(); idx++) {
    GCObject* node = g_stack_roots.at(idx);
    if (!node->isTrackable()) {
      GCRuntime::onAssignRootVariable_internal(node);
      closure->do_iterate(cast_to_oop(node));
    }
    g_resurrected_top = INT_MAX; 
  }
}

void rtHeap_checkWeakReachable(oopDesc* p) {
  precond(to_obj(p)->getRootRefCount() > 0);
}

static int cc = 0;
void check_no_ref(oopDesc* p) {
  cc ++;
  if (to_obj(p)->getRootRefCount() > 0) {
    rtgc_log(true, "clear-ref %p[%d]\n", p, cc);//to_obj(p)->getRootRefCount());
    // to_obj(p)->decrementRootRefCount();
  } else {
    // rtgc_log(true, "no-ref %p[%s]\n", p, RTGC::getClassName(to_obj(p)));
  }
  if ((cc % 500) == 0) {
    rtgc_log(true, "cc-ref %p[%d]\n", p, cc);//to_obj(p)->getRootRefCount());
  }
}