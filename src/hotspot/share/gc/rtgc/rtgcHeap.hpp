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
class DefNewYoungerGenClosure;

class rtHeap : AllStatic {
public:
  static bool is_trackable(oopDesc* p);
  static bool is_destroyed(oopDesc* p);
  static bool is_alive(oopDesc* p, bool assert_alive = false);
  static void prepare_rtgc(bool isTenured);

  // for younger object collection
  static void mark_promoted_trackable(oopDesc* new_p);
  static void add_promoted_link(oopDesc* promoted_anchor, oopDesc* linked, bool link_is_tenured);
  static void iterate_young_roots(BoolObjectClosure* young_root_closure, OopClosure* survivor_closure);
  static void mark_survivor_reachable(oopDesc* tenured_p, bool as_java_referent = false);
  static void mark_young_root_reachable(oopDesc* referent);
  static void add_young_root(oopDesc* old_p, oopDesc* new_p);
  static bool is_valid_link_of_yg_root(oopDesc* yg_root, oopDesc* link);
  static void oop_recycled_iterate(DefNewYoungerGenClosure* closure);
  static void mark_weak_reachable(oopDesc* new_p);
  static void clear_weak_reachable(oopDesc* new_p);

  // for full gc
  static void prepare_point_adjustment();
  static size_t adjust_pointers(oopDesc* old_p);
  static void mark_pending_trackable(oopDesc* old_p, void* new_p);
  static void mark_forwarded(oopDesc* p);
  static void destroy_trackable(oopDesc* p);

  // for jni
  static void lock_jni_handle(oopDesc* p);
  static void release_jni_handle(oopDesc* p);

  // for reference management
  static void init_java_reference(oopDesc* ref_oop, oopDesc* referent);
  static void link_discovered_pending_reference(oopDesc* ref_oop, oopDesc* discovered);
  static bool is_active_finalizere_reachable(oopDesc* javaReference);
  static void process_java_references(OopClosure* keep_alive, VoidClosure* complete_gc, ReferenceType clear_ref);
  static void finish_compaction_gc(bool is_tenure_gc);

  // just for debugging
  static void print_heap_after_gc(bool full_gc);
  static void mark_empty_trackable(oopDesc* p);
  static HeapWord* allocate_tlab(Thread* thread, const size_t word_size);
};



#endif // SHARE_GC_RTGC_RTGCHEAP_HPP