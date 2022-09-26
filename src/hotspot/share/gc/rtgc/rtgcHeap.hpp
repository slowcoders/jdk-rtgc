#ifndef SHARE_GC_RTGC_RTGCHEAP_HPP
#define SHARE_GC_RTGC_RTGCHEAP_HPP

#include "utilities/macros.hpp"
#include "memory/allStatic.hpp"
#include "memory/referenceType.hpp"
#include "gc/shared/gc_globals.hpp"

#include "rtgcDebug.hpp"

class Thread;
class oopDesc;
class OopIterateClosure;
class BoolObjectClosure;
class OopClosure;
class ReferencePolicy;
class PromotedTrackableClosure;

class RtYoungRootClosure {
public:  
  virtual bool iterate_tenured_young_root_oop(oop root) = 0;
  virtual void do_complete() = 0;
};

class rtHeap : AllStatic {
public:
  static int  DoCrossCheck;
  static bool in_full_gc;
  static bool is_trackable(oopDesc* p);
  static bool is_alive(oopDesc* p);

  static void prepare_rtgc(bool is_full_gc);
  static void iterate_younger_gen_roots(RtYoungRootClosure* young_root_closure, bool is_full_gc);
  static void finish_rtgc();

  // for younger object collection
  static void add_promoted_trackable(oopDesc* new_p);
  static void mark_trackable(oopDesc* new_p);
  static void add_trackable_link(oopDesc* promoted_anchor, oopDesc* linked);
  static void mark_survivor_reachable(oopDesc* tenured_p);

  static void add_young_root(oopDesc* old_p, oopDesc* new_p);
  static void oop_recycled_iterate(PromotedTrackableClosure* closure);

  // for full gc
  static void mark_forwarded(oopDesc* p);
  static void destroy_trackable(oopDesc* p);
  static void prepare_adjust_pointers(HeapWord* old_gen_heap_start);
  static size_t adjust_pointers(oopDesc* old_p);
  static void finish_adjust_pointers();

  // for jni
  static void lock_jni_handle(oopDesc* p);
  static void release_jni_handle(oopDesc* p);
  static void mark_weak_reachable(oopDesc* new_p);
  static void clear_weak_reachable(oopDesc* new_p);
  static bool ensure_weak_reachable(oopDesc* new_p);

  // for reference management
  static void init_java_reference(oopDesc* ref_oop, oopDesc* referent);
  static void link_discovered_pending_reference(oopDesc* ref_oop, oopDesc* discovered);
  static bool is_active_finalizer_reachable(oopDesc* final_referent);
  static void process_weak_soft_references(OopClosure* keep_alive, VoidClosure* complete_gc, ReferencePolicy* policy);
  static void process_final_phantom_references(bool is_tenure_gc);

  // just for debugging
  static void print_heap_after_gc(bool full_gc);
  static HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
};



#endif // SHARE_GC_RTGC_RTGCHEAP_HPP