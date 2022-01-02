/*
 * Copyright (c) 2015, 2019, Oracle and/or its affiliates. All rights reserved.
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
 */

#ifndef SHARE_GC_RTGC_RTGCBARRIER_INLINE_HPP
#define SHARE_GC_RTGC_RTGCBARRIER_INLINE_HPP

#include "classfile/javaClasses.hpp"
// #include "gc/z/zAddress.inline.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
// #include "gc/z/zOop.inline.hpp"
// #include "gc/z/zResurrection.inline.hpp"
#include "oops/oop.hpp"
#include "runtime/atomic.hpp"

// // A self heal must always "upgrade" the address metadata bits in
// // accordance with the metadata bits state machine, which has the
// // valid state transitions as described below (where N is the GC
// // cycle).
// //
// // Note the subtleness of overlapping GC cycles. Specifically that
// // oops are colored Remapped(N) starting at relocation N and ending
// // at marking N + 1.
// //
// //              +--- Mark Start
// //              | +--- Mark End
// //              | | +--- Relocate Start
// //              | | | +--- Relocate End
// //              | | | |
// // Marked       |---N---|--N+1--|--N+2--|----
// // Finalizable  |---N---|--N+1--|--N+2--|----
// // Remapped     ----|---N---|--N+1--|--N+2--|
// //
// // VALID STATE TRANSITIONS
// //
// //   Marked(N)           -> Remapped(N)
// //                       -> Marked(N + 1)
// //                       -> Finalizable(N + 1)
// //
// //   Finalizable(N)      -> Marked(N)
// //                       -> Remapped(N)
// //                       -> Marked(N + 1)
// //                       -> Finalizable(N + 1)
// //
// //   Remapped(N)         -> Marked(N + 1)
// //                       -> Finalizable(N + 1)
// //
// // PHASE VIEW
// //
// // ZPhaseMark
// //   Load & Mark
// //     Marked(N)         <- Marked(N - 1)
// //                       <- Finalizable(N - 1)
// //                       <- Remapped(N - 1)
// //                       <- Finalizable(N)
// //
// //   Mark(Finalizable)
// //     Finalizable(N)    <- Marked(N - 1)
// //                       <- Finalizable(N - 1)
// //                       <- Remapped(N - 1)
// //
// //   Load(AS_NO_KEEPALIVE)
// //     Remapped(N - 1)   <- Marked(N - 1)
// //                       <- Finalizable(N - 1)
// //
// // ZPhaseMarkCompleted (Resurrection blocked)
// //   Load & Load(ON_WEAK/PHANTOM_OOP_REF | AS_NO_KEEPALIVE) & KeepAlive
// //     Marked(N)         <- Marked(N - 1)
// //                       <- Finalizable(N - 1)
// //                       <- Remapped(N - 1)
// //                       <- Finalizable(N)
// //
// //   Load(ON_STRONG_OOP_REF | AS_NO_KEEPALIVE)
// //     Remapped(N - 1)   <- Marked(N - 1)
// //                       <- Finalizable(N - 1)
// //
// // ZPhaseMarkCompleted (Resurrection unblocked)
// //   Load
// //     Marked(N)         <- Finalizable(N)
// //
// // ZPhaseRelocate
// //   Load & Load(AS_NO_KEEPALIVE)
// //     Remapped(N)       <- Marked(N)
// //                       <- Finalizable(N)

// template <RtgcBarrierFastPath fast_path>
// inline void RtgcBarrier::self_heal(volatile oop* p, uintptr_t addr, uintptr_t heal_addr) {
//   if (heal_addr == 0) {
//     // Never heal with null since it interacts badly with reference processing.
//     // A mutator clearing an oop would be similar to calling Reference.clear(),
//     // which would make the reference non-discoverable or silently dropped
//     // by the reference processor.
//     return;
//   }

//   assert(!fast_path(addr), "Invalid self heal");
//   assert(fast_path(heal_addr), "Invalid self heal");

//   for (;;) {
//     // Heal
//     const uintptr_t prev_addr = Atomic::cmpxchg((volatile uintptr_t*)p, addr, heal_addr);
//     if (prev_addr == addr) {
//       // Success
//       return;
//     }

//     if (fast_path(prev_addr)) {
//       // Must not self heal
//       return;
//     }

//     // The oop location was healed by another barrier, but still needs upgrading.
//     // Re-apply healing to make sure the oop is not left with weaker (remapped or
//     // finalizable) metadata bits than what this barrier tried to apply.
//     assert(ZAddress::offset(prev_addr) == ZAddress::offset(heal_addr), "Invalid offset");
//     addr = prev_addr;
//   }
// }

template <RtgcBarrierFastPath fast_path, RtgcBarrierSlowPath slow_path>
inline oop RtgcBarrier::barrier(volatile oop* p, oop o) {
  return o;
}

template <RtgcBarrierFastPath fast_path, RtgcBarrierSlowPath slow_path>
inline oop RtgcBarrier::weak_barrier(volatile oop* p, oop o) {
  return o;
}

template <RtgcBarrierFastPath fast_path, RtgcBarrierSlowPath slow_path>
inline void RtgcBarrier::root_barrier(oop* p, oop o) {
  return o;
}

inline bool RtgcBarrier::is_good_or_null_fast_path(uintptr_t addr) {
  return true;//ZAddress::is_good_or_null(addr);
}

inline bool RtgcBarrier::is_weak_good_or_null_fast_path(uintptr_t addr) {
  return true;//ZAddress::is_weak_good_or_null(addr);
}

inline bool RtgcBarrier::is_marked_or_null_fast_path(uintptr_t addr) {
  return true;//ZAddress::is_marked_or_null(addr);
}

inline bool RtgcBarrier::during_mark() {
  return true;//ZGlobalPhase == ZPhaseMark;
}

inline bool RtgcBarrier::during_relocate() {
  return true;//ZGlobalPhase == ZPhaseRelocate;
}

//
// Load barrier
//
inline oop RtgcBarrier::load_barrier_on_oop(oop o) {
  return load_barrier_on_oop_field_preloaded((oop*)NULL, o);
}

inline oop RtgcBarrier::load_barrier_on_oop_field(volatile oop* p) {
  const oop o = *p;
  return load_barrier_on_oop_field_preloaded(p, o);
}

inline oop RtgcBarrier::load_barrier_on_oop_field_preloaded(volatile oop* p, oop o) {
  return barrier<is_good_or_null_fast_path, load_barrier_on_oop_slow_path>(p, o);
}

inline void RtgcBarrier::load_barrier_on_oop_array(volatile oop* p, size_t length) {
  for (volatile const oop* const end = p + length; p < end; p++) {
    load_barrier_on_oop_field(p);
  }
}

// ON_WEAK barriers should only ever be applied to j.l.r.Reference.referents.
inline void verify_on_weak(volatile oop* referent_addr) {
#ifdef ASSERT
  if (referent_addr != NULL) {
    uintptr_t base = (uintptr_t)referent_addr - java_lang_ref_Reference::referent_offset;
    oop obj = cast_to_oop(base);
    assert(oopDesc::is_oop(obj), "Verification failed for: ref " PTR_FORMAT " obj: " PTR_FORMAT, (uintptr_t)referent_addr, base);
    assert(java_lang_ref_Reference::is_referent_field(obj, java_lang_ref_Reference::referent_offset), "Sanity");
  }
#endif
}

inline oop RtgcBarrier::load_barrier_on_weak_oop_field_preloaded(volatile oop* p, oop o) {
  verify_on_weak(p);

  // if (ZResurrection::is_blocked()) {
  //   return barrier<is_good_or_null_fast_path, weak_load_barrier_on_weak_oop_slow_path>(p, o);
  // }

  return load_barrier_on_oop_field_preloaded(p, o);
}

inline oop RtgcBarrier::load_barrier_on_phantom_oop_field_preloaded(volatile oop* p, oop o) {
  // if (ZResurrection::is_blocked()) {
  //   return barrier<is_good_or_null_fast_path, weak_load_barrier_on_phantom_oop_slow_path>(p, o);
  // }

  return load_barrier_on_oop_field_preloaded(p, o);
}

inline void RtgcBarrier::load_barrier_on_root_oop_field(oop* p) {
  const oop o = *p;
  root_barrier<is_good_or_null_fast_path, load_barrier_on_oop_slow_path>(p, o);
}

//
// Weak load barrier
//
inline oop RtgcBarrier::weak_load_barrier_on_oop_field(volatile oop* p) {
  assert(!ZResurrection::is_blocked(), "Should not be called during resurrection blocked phase");
  const oop o = *p;
  return weak_load_barrier_on_oop_field_preloaded(p, o);
}

inline oop RtgcBarrier::weak_load_barrier_on_oop_field_preloaded(volatile oop* p, oop o) {
  return weak_barrier<is_weak_good_or_null_fast_path, weak_load_barrier_on_oop_slow_path>(p, o);
}

inline oop RtgcBarrier::weak_load_barrier_on_weak_oop(oop o) {
  return weak_load_barrier_on_weak_oop_field_preloaded((oop*)NULL, o);
}

inline oop RtgcBarrier::weak_load_barrier_on_weak_oop_field(volatile oop* p) {
  const oop o = *p;
  return weak_load_barrier_on_weak_oop_field_preloaded(p, o);
}

inline oop RtgcBarrier::weak_load_barrier_on_weak_oop_field_preloaded(volatile oop* p, oop o) {
  verify_on_weak(p);

  if (ZResurrection::is_blocked()) {
    return barrier<is_good_or_null_fast_path, weak_load_barrier_on_weak_oop_slow_path>(p, o);
  }

  return weak_load_barrier_on_oop_field_preloaded(p, o);
}

inline oop RtgcBarrier::weak_load_barrier_on_phantom_oop(oop o) {
  return weak_load_barrier_on_phantom_oop_field_preloaded((oop*)NULL, o);
}

inline oop RtgcBarrier::weak_load_barrier_on_phantom_oop_field(volatile oop* p) {
  const oop o = *p;
  return weak_load_barrier_on_phantom_oop_field_preloaded(p, o);
}

inline oop RtgcBarrier::weak_load_barrier_on_phantom_oop_field_preloaded(volatile oop* p, oop o) {
  if (ZResurrection::is_blocked()) {
    return barrier<is_good_or_null_fast_path, weak_load_barrier_on_phantom_oop_slow_path>(p, o);
  }

  return weak_load_barrier_on_oop_field_preloaded(p, o);
}

//
// Is alive barrier
//
inline bool RtgcBarrier::is_alive_barrier_on_weak_oop(oop o) {
  // Check if oop is logically non-null. This operation
  // is only valid when resurrection is blocked.
  assert(ZResurrection::is_blocked(), "Invalid phase");
  return weak_load_barrier_on_weak_oop(o) != NULL;
}

inline bool RtgcBarrier::is_alive_barrier_on_phantom_oop(oop o) {
  // Check if oop is logically non-null. This operation
  // is only valid when resurrection is blocked.
  assert(ZResurrection::is_blocked(), "Invalid phase");
  return weak_load_barrier_on_phantom_oop(o) != NULL;
}

//
// Keep alive barrier
//
inline void RtgcBarrier::keep_alive_barrier_on_weak_oop_field(volatile oop* p) {
  // This operation is only valid when resurrection is blocked.
  assert(ZResurrection::is_blocked(), "Invalid phase");
  const oop o = *p;
  barrier<is_good_or_null_fast_path, keep_alive_barrier_on_weak_oop_slow_path>(p, o);
}

inline void RtgcBarrier::keep_alive_barrier_on_phantom_oop_field(volatile oop* p) {
  // This operation is only valid when resurrection is blocked.
  assert(ZResurrection::is_blocked(), "Invalid phase");
  const oop o = *p;
  barrier<is_good_or_null_fast_path, keep_alive_barrier_on_phantom_oop_slow_path>(p, o);
}

inline void RtgcBarrier::keep_alive_barrier_on_phantom_root_oop_field(oop* p) {
  // This operation is only valid when resurrection is blocked.
  assert(ZResurrection::is_blocked(), "Invalid phase");
  const oop o = *p;
  root_barrier<is_good_or_null_fast_path, keep_alive_barrier_on_phantom_oop_slow_path>(p, o);
}

inline void RtgcBarrier::keep_alive_barrier_on_oop(oop o) {
  const uintptr_t addr = ZOop::to_address(o);
  assert(ZAddress::is_good(addr), "Invalid address");

  if (during_mark()) {
    mark_barrier_on_oop_slow_path(addr);
  }
}

//
// Mark barrier
//
inline void RtgcBarrier::mark_barrier_on_oop_field(volatile oop* p, bool finalizable) {
  const oop o = *p;

  if (finalizable) {
    barrier<is_marked_or_null_fast_path, mark_barrier_on_finalizable_oop_slow_path>(p, o);
  } else {
    const uintptr_t addr = ZOop::to_address(o);
    if (ZAddress::is_good(addr)) {
      // Mark through good oop
      mark_barrier_on_oop_slow_path(addr);
    } else {
      // Mark through bad oop
      barrier<is_good_or_null_fast_path, mark_barrier_on_oop_slow_path>(p, o);
    }
  }
}

inline void RtgcBarrier::mark_barrier_on_oop_array(volatile oop* p, size_t length, bool finalizable) {
  for (volatile const oop* const end = p + length; p < end; p++) {
    mark_barrier_on_oop_field(p, finalizable);
  }
}

inline void RtgcBarrier::mark_barrier_on_root_oop_field(oop* p) {
  const oop o = *p;
  root_barrier<is_good_or_null_fast_path, mark_barrier_on_root_oop_slow_path>(p, o);
}

inline void RtgcBarrier::mark_barrier_on_invisible_root_oop_field(oop* p) {
  const oop o = *p;
  root_barrier<is_good_or_null_fast_path, mark_barrier_on_invisible_root_oop_slow_path>(p, o);
}

//
// Relocate barrier
//
inline void RtgcBarrier::relocate_barrier_on_root_oop_field(oop* p) {
  const oop o = *p;
  root_barrier<is_good_or_null_fast_path, relocate_barrier_on_root_oop_slow_path>(p, o);
}

#endif // SHARE_GC_RTGC_RTGCBARRIER_INLINE_HPP
