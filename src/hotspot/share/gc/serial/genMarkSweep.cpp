/*
 * Copyright (c) 2001, 2021, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/stringTable.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "code/codeCache.hpp"
#include "code/icBuffer.hpp"
#include "compiler/oopMap.hpp"
#include "gc/serial/genMarkSweep.hpp"
#include "gc/serial/serialGcRefProcProxyTask.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "gc/shared/gcHeapSummary.hpp"
#include "gc/shared/gcTimer.hpp"
#include "gc/shared/gcTrace.hpp"
#include "gc/shared/gcTraceTime.inline.hpp"
#include "gc/shared/genCollectedHeap.hpp"
#include "gc/shared/generation.hpp"
#include "gc/shared/genOopClosures.inline.hpp"
#include "gc/shared/modRefBarrierSet.hpp"
#include "gc/shared/referencePolicy.hpp"
#include "gc/shared/referenceProcessorPhaseTimes.hpp"
#include "gc/shared/space.hpp"
#include "gc/shared/strongRootsScope.hpp"
#include "gc/shared/weakProcessor.hpp"
#include "memory/universe.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceRefKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/thread.inline.hpp"
#include "runtime/vmThread.hpp"
#include "utilities/copy.hpp"
#include "utilities/events.hpp"
#include "utilities/stack.inline.hpp"
#include "gc/rtgc/rtgcHeap.hpp"
#if INCLUDE_JVMCI
#include "jvmci/jvmci.hpp"
#endif

void GenMarkSweep::invoke_at_safepoint(ReferenceProcessor* rp, bool clear_all_softrefs) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at a safepoint");

  GenCollectedHeap* gch = GenCollectedHeap::heap();
#ifdef ASSERT
  if (gch->soft_ref_policy()->should_clear_all_soft_refs()) {
    assert(clear_all_softrefs, "Policy should have been checked earlier");
  }
#endif

  // hook up weak ref data so it can be used during Mark-Sweep
  assert(ref_processor() == NULL, "no stomping");
  assert(rp != NULL, "should be non-NULL");
  set_ref_processor(rp);
  ReferencePolicy* ref_policy = rp->setup_policy(clear_all_softrefs);

  gch->trace_heap_before_gc(_gc_tracer);

  // Increment the invocation count
  _total_invocations++;

  // Capture used regions for each generation that will be
  // subject to collection, so that card table adjustments can
  // be made intelligently (see clear / invalidate further below).
  gch->save_used_regions();

  allocate_stacks();

#if INCLUDE_RTGC 
  if (EnableRTGC) {
    precond(ref_policy != NULL);
    rtHeap::prepare_rtgc(ref_policy);
  }
#endif

  mark_sweep_phase1(clear_all_softrefs);

  mark_sweep_phase2();

  // Don't add any more derived pointers during phase3
#if COMPILER2_OR_JVMCI
  assert(DerivedPointerTable::is_active(), "Sanity");
  DerivedPointerTable::set_active(false);
#endif

#if INCLUDE_RTGC
  if (EnableRTGC) {
    HeapWord* old_gen_heap_start = GenCollectedHeap::heap()->old_gen()->reserved().start();
    rtHeap::prepare_adjust_pointers(old_gen_heap_start);
  }
#endif

  mark_sweep_phase3();

#if INCLUDE_RTGC // RTGC_OPT_YOUNG_ROOTS
  if (EnableRTGC) {
    rtHeap::finish_adjust_pointers();
  }
#endif

  mark_sweep_phase4();

#if INCLUDE_RTGC
  if (EnableRTGC) { 
    rtHeap::finish_rtgc(true, true);
  }
#endif

  restore_marks();

  // Set saved marks for allocation profiler (and other things? -- dld)
  // (Should this be in general part?)
  gch->save_marks();

  deallocate_stacks();

  // If compaction completely evacuated the young generation then we
  // can clear the card table.  Otherwise, we must invalidate
  // it (consider all cards dirty).  In the future, we might consider doing
  // compaction within generations only, and doing card-table sliding.
  CardTableRS* rs = gch->rem_set();
  Generation* old_gen = gch->old_gen();

  RTGC_ONLY(if (!RtNoDirtyCardMarking)) {
    // Clear/invalidate below make use of the "prev_used_regions" saved earlier.
    if (gch->young_gen()->used() == 0) {
      // We've evacuated the young generation.
      rs->clear_into_younger(old_gen);
    } else {
      // Invalidate the cards corresponding to the currently used
      // region and clear those corresponding to the evacuated region.
      rs->invalidate_or_clear(old_gen);
    }
  }

  gch->prune_scavengable_nmethods();

  // refs processing: clean slate
  set_ref_processor(NULL);

  // Update heap occupancy information which is used as
  // input to soft ref clearing policy at the next gc.
  Universe::heap()->update_capacity_and_used_at_gc();

  // Signal that we have completed a visit to all live objects.
  Universe::heap()->record_whole_heap_examined_timestamp();

  gch->trace_heap_after_gc(_gc_tracer);
}

void GenMarkSweep::allocate_stacks() {
  GenCollectedHeap* gch = GenCollectedHeap::heap();
  // Scratch request on behalf of old generation; will do no allocation.
  ScratchBlock* scratch = gch->gather_scratch(gch->old_gen(), 0);

  // $$$ To cut a corner, we'll only use the first scratch block, and then
  // revert to malloc.
  if (scratch != NULL) {
    _preserved_count_max =
      scratch->num_words * HeapWordSize / sizeof(PreservedMark);
  } else {
    _preserved_count_max = 0;
  }

  _preserved_marks = (PreservedMark*)scratch;
  _preserved_count = 0;
}


void GenMarkSweep::deallocate_stacks() {
  GenCollectedHeap* gch = GenCollectedHeap::heap();
  gch->release_scratch();

  _preserved_mark_stack.clear(true);
  _preserved_oop_stack.clear(true);
  _marking_stack.clear();
  _objarray_stack.clear(true);
}

#if INCLUDE_RTGC // RTGC_OPT_YOUNG_ROOTS
class TenuredYoungRootClosure : public MarkAndPushClosure, public RtYoungRootClosure {
  bool _is_young_root;
public:
  
  bool iterate_tenured_young_root_oop(oopDesc* obj) {
    precond(rtHeap::is_trackable(obj));
    if (rtHeap::DoCrossCheck && obj->is_gc_marked()) {
      return true;
    }
    _is_young_root = false;
    oop old_anchor = _current_anchor;
    _current_anchor = obj;
    obj->oop_iterate(this);
    _current_anchor = old_anchor;
    return _is_young_root;
  }

  void do_complete() {
    MarkSweep::follow_stack();
  }

  template <typename T>
  void do_oop_work(T* p) {
    T heap_oop = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(heap_oop)) {
      oop obj = CompressedOops::decode_not_null(heap_oop);
      if (!rtHeap::is_trackable(obj)) {
        _is_young_root = true;
      } else {
        if (!rtHeap::is_alive(obj)) {
          rtHeap::mark_survivor_reachable(obj);
        } 
        if (!rtHeap::DoCrossCheck) return;
      }
      MarkSweep::mark_and_push_internal(obj, true);
    }
  }

  virtual void do_oop(oop* p) { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p); }

};
static TenuredYoungRootClosure young_root_closure;
#endif

void GenMarkSweep::mark_sweep_phase1(bool clear_all_softrefs) {
  // Recursively traverse all live objects and mark them
  GCTraceTime(Info, gc, phases) tm("Phase 1: Mark live objects", _gc_timer);

  GenCollectedHeap* gch = GenCollectedHeap::heap();

  // Need new claim bits before marking starts.
  ClassLoaderDataGraph::clear_claimed_marks();

  {
    StrongRootsScope srs(0);

    gch->full_process_roots(false, // not the adjust phase
                            GenCollectedHeap::SO_None,
                            NULL, //ClassUnloading RTGC_ONLY(? &release_weak_cld_rtgc_ref : NULL), // only strong roots if ClassUnloading
                                            // is enabled
                            &follow_root_closure,
                            &follow_cld_closure);
  }

#if INCLUDE_RTGC // RTGC_OPT_YOUNG_ROOTS
  if (EnableRTGC) {
    if (true || !rtHeap::DoCrossCheck) {
      young_root_closure.set_ref_discoverer(_ref_processor);
      rtHeap::iterate_younger_gen_roots(&young_root_closure, true);
    }
    rtHeap::process_weak_soft_references(&keep_alive, &follow_stack_closure, true);
  }
#endif
  // Process reference objects found during marking
  if (!EnableRTGC || rtHeap::DoCrossCheck) {
    GCTraceTime(Debug, gc, phases) tm_m("Reference Processing", gc_timer());

    ref_processor()->setup_policy(clear_all_softrefs);
    ReferenceProcessorPhaseTimes pt(_gc_timer, ref_processor()->max_num_queues());
    SerialGCRefProcProxyTask task(is_alive, keep_alive, follow_stack_closure);
    const ReferenceProcessorStats& stats = ref_processor()->process_discovered_references(task, pt);
    pt.print_all_references();
    gc_tracer()->report_gc_reference_stats(stats);
  }

  // This is the point where the entire marking should have completed.
  assert(_marking_stack.is_empty(), "Marking should have completed");

#if INCLUDE_RTGC // RTGC_OPT_YOUNG_ROOTS
  if (EnableRTGC) {
    rtHeap::process_final_phantom_references(&keep_alive, &follow_stack_closure, true);
    assert(_marking_stack.is_empty(), "Marking should have completed");
  }
#endif

  GCTraceTime(Debug, gc, phases) tm_m("Weak Processing", gc_timer());
  WeakProcessor::weak_oops_do(&is_alive, &do_nothing_cl);
  
  {
    GCTraceTime(Debug, gc, phases) tm_m("Class Unloading", gc_timer());

    // Unload classes and purge the SystemDictionary.
    bool purged_class = SystemDictionary::do_unloading(gc_timer());

    // Unload nmethods.
    CodeCache::do_unloading(&is_alive, purged_class);

    // Prune dead klasses from subklass/sibling/implementor lists.
    Klass::clean_weak_klass_links(purged_class);

    // Clean JVMCI metadata handles.
    JVMCI_ONLY(JVMCI::do_unloading(purged_class));
  }

  gc_tracer()->report_object_count_after_gc(&is_alive);
}


void GenMarkSweep::mark_sweep_phase2() {
  // Now all live objects are marked, compute the new object addresses.

  // It is imperative that we traverse perm_gen LAST. If dead space is
  // allowed a range of dead object may get overwritten by a dead int
  // array. If perm_gen is not traversed last a Klass* may get
  // overwritten. This is fine since it is dead, but if the class has dead
  // instances we have to skip them, and in order to find their size we
  // need the Klass*!
  //
  // It is not required that we traverse spaces in the same order in
  // phase2, phase3 and phase4, but the ValidateMarkSweep live oops
  // tracking expects us to do so. See comment under phase4.

  GenCollectedHeap* gch = GenCollectedHeap::heap();

  GCTraceTime(Info, gc, phases) tm("Phase 2: Compute new object addresses", _gc_timer);

  gch->prepare_for_compaction();
}

class GenAdjustPointersClosure: public GenCollectedHeap::GenClosure {
public:
  void do_generation(Generation* gen) {
    gen->adjust_pointers();
  }
};

void GenMarkSweep::mark_sweep_phase3() {
  GenCollectedHeap* gch = GenCollectedHeap::heap();

  // Adjust the pointers to reflect the new locations
  GCTraceTime(Info, gc, phases) tm("Phase 3: Adjust pointers", gc_timer());

  // Need new claim bits for the pointer adjustment tracing.
  ClassLoaderDataGraph::clear_claimed_marks();

  {
    StrongRootsScope srs(0);

    gch->full_process_roots(true,  // this is the adjust phase
                            GenCollectedHeap::SO_AllCodeCache,
                            RTGC_ONLY(&adjust_cld_closure) NOT_RTGC(false), // all roots
                            &adjust_pointer_closure,
                            &adjust_cld_closure);
  }

  gch->gen_process_weak_roots(&adjust_pointer_closure);

  adjust_marks();
  GenAdjustPointersClosure blk;
  gch->generation_iterate(&blk, true);
}

class GenCompactClosure: public GenCollectedHeap::GenClosure {
public:
  void do_generation(Generation* gen) {
    gen->compact();
  }
};

void GenMarkSweep::mark_sweep_phase4() {
  // All pointers are now adjusted, move objects accordingly

  // It is imperative that we traverse perm_gen first in phase4. All
  // classes must be allocated earlier than their instances, and traversing
  // perm_gen first makes sure that all Klass*s have moved to their new
  // location before any instance does a dispatch through it's klass!

  // The ValidateMarkSweep live oops tracking expects us to traverse spaces
  // in the same order in phase2, phase3 and phase4. We don't quite do that
  // here (perm_gen first rather than last), so we tell the validate code
  // to use a higher index (saved from phase2) when verifying perm_gen.
  GenCollectedHeap* gch = GenCollectedHeap::heap();

  GCTraceTime(Info, gc, phases) tm("Phase 4: Move objects", _gc_timer);

  GenCompactClosure blk;
  gch->generation_iterate(&blk, true);
}
