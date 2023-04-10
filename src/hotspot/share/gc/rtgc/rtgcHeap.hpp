#ifndef SHARE_GC_RTGC_RTGCHEAP_HPP
#define SHARE_GC_RTGC_RTGCHEAP_HPP

#include "utilities/macros.hpp"
#include "memory/allStatic.hpp"
#include "memory/referenceType.hpp"
#include "gc/shared/gc_globals.hpp"
#include "oops/oopsHierarchy.hpp"

#include "rtgcDebug.hpp"

#define RTGC_FAT_OOP        true
#define RTGC_SHARE_GC_MARK  false
#define RTGC_ENABLE_ACYCLIC_REF_COUNT   false
#define AUTO_TRACKABLE_MARK_BY_ADDRESS  true

class Thread;
class oopDesc;
class ObjectClosure;
class OopIterateClosure;
class BoolObjectClosure;
class OopClosure;
class VoidClosure;
class ReferencePolicy;
class ReferenceDiscoverer;

class RtYoungRootClosure {
protected:
  oopDesc* _current_anchor;
public:  
  RtYoungRootClosure() : _current_anchor(0) {}

  virtual bool iterate_tenured_young_root_oop(oopDesc* root, bool is_root_reachable) = 0;
  virtual void do_complete(bool is_strong_rechable) = 0;
  virtual oop  keep_alive_young_referent(oop p) = 0;
};

class rtHeap : AllStatic {
public:
  static int  DoCrossCheck;
  static int  in_full_gc;

  static void init_allocated_object(HeapWord* mem, Klass* klass);

  static void init_mark(oopDesc* p);
  static bool is_trackable(oopDesc* p);
  static bool is_alive(oopDesc* p, bool must_not_destroyed = true);
  static bool is_destroyed(oopDesc* p);
  static bool is_in_trackable_space(void* p);

  static void prepare_rtgc();
  static void init_reference_processor(ReferencePolicy* policy);
  static void iterate_younger_gen_roots(RtYoungRootClosure* young_root_closure, bool is_full_gc);
  static void finish_rtgc(bool is_full_gc, bool promotion_finished);

  static void mark_promoted_trackable(oopDesc* new_p);
  static void mark_young_root_reachable(oopDesc* anchor, oopDesc* link);
  static void mark_young_survivor_reachable(oopDesc* anchor, oopDesc* link);
  static void add_trackable_link(oopDesc* promoted_anchor, oopDesc* linked);
  static void ensure_trackable_link(oopDesc* anchor, oopDesc* obj);
  static void clear_temporal_anchor_list(oopDesc* oop);

  static void mark_survivor_reachable(oopDesc* tenured_p);
  static void mark_resurrected_link(oopDesc* resurrected_anchor, oopDesc* tenured_p);

  static void add_young_root(oopDesc* old_p, oopDesc* new_p);
  static void mark_young_root(oopDesc* tenured_p, bool is_young_root);
  static void oop_recycled_iterate(ObjectClosure* closure);

  static void mark_dead_space(oopDesc* obj);
  
  static void mark_forwarded_trackable(oopDesc* p);
  static void destroy_trackable(oopDesc* p);
  static void prepare_adjust_pointers(HeapWord* old_gen_heap_start);
  static size_t adjust_pointers(oopDesc* old_p);
  static void finish_adjust_pointers();

  // for jni
  static void lock_jni_handle(oopDesc* p);
  static void release_jni_handle(oopDesc* p);
  static void lock_jni_handle_at_safepoint(oopDesc* p);
  static void release_jni_handle_at_safepoint(oopDesc* p);

  // for reference management
  static bool try_discover(oopDesc* ref, ReferenceType ref_type, ReferenceDiscoverer* refDiscoverer);
  static bool is_referent_reachable(oopDesc* ref, ReferenceType type);
  static void init_java_reference(oopDesc* ref, oopDesc* referent);
  static void link_discovered_pending_reference(oopDesc* ref, oopDesc* discovered);
  static void process_weak_soft_references(OopClosure* keep_alive, VoidClosure* complete_gc, bool is_full_gc);
  static void process_final_phantom_references(OopClosure* keep_alive, VoidClosure* complete_gc, bool is_full_gc);

  // just for debugging
  static void print_heap_after_gc(bool full_gc);
  static HeapWord* allocate_tlab(Thread* thread, const size_t word_size);

#ifdef ASSERT
  static bool useModifyFlag();
#else 
  static bool useModifyFlag() { return true; }
#endif

  static inline bool is_modified(narrowOop p) {
    return (((uint32_t)p) & 1) != 0;
  }

  static inline bool is_modified(oop p) {
    return (((uintptr_t)(void*)p) & 1) != 0;
  }

  static inline narrowOop to_modified(narrowOop p) {
    return (narrowOop)(((uint32_t)p) | 1);
  }

  static inline oop to_modified(oop p) {
    return cast_to_oop(((uintptr_t)(void*)p) | 1);
  }

  static inline narrowOop to_unmodified(narrowOop p) {
    return (narrowOop)(((uint32_t)p) & ~1);
  }

  static inline oop to_unmodified(oop p) {
    return cast_to_oop(((uintptr_t)(void*)p) & ~1);
  }

};



#endif // SHARE_GC_RTGC_RTGCHEAP_HPP