/*
 * Copyright (c) 2017, 2018, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_GC_RTGC_RTGCBARRIERSET_HPP
#define SHARE_GC_RTGC_RTGCBARRIERSET_HPP

#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/barrierSet.hpp"

// No interaction with application is required for Rtgc, and therefore
// the barrier set is empty.
class RtgcBarrierSet: public BarrierSet {
  friend class VMStructs;

public:
  RtgcBarrierSet();

  virtual void print_on(outputStream *st) const {}

  virtual void on_thread_create(Thread* thread);
  virtual void on_thread_destroy(Thread* thread);

  template <DecoratorSet decorators, typename BarrierSetT = RtgcBarrierSet>
  class AccessBarrier: public BarrierSet::AccessBarrier<decorators, BarrierSetT> {
  private:
    typedef BarrierSet::AccessBarrier<decorators, BarrierSetT> Raw;

    template <DecoratorSet expected>
    static void verify_decorators_present();

    template <DecoratorSet expected>
    static void verify_decorators_absent();

    static oop* field_addr(oop base, ptrdiff_t offset);

  public:
    //
    // In heap
    //
    template <typename T>
    static oop oop_load_in_heap(T* addr);
    static oop oop_load_in_heap_at(oop base, ptrdiff_t offset);

    template <typename T>
    static oop oop_atomic_cmpxchg_in_heap(T* addr, oop compare_value, oop new_value);
    static oop oop_atomic_cmpxchg_in_heap_at(oop base, ptrdiff_t offset, oop compare_value, oop new_value);

    template <typename T>
    static oop oop_atomic_xchg_in_heap(T* addr, oop new_value);
    static oop oop_atomic_xchg_in_heap_at(oop base, ptrdiff_t offset, oop new_value);

    template <typename T>
    static bool oop_arraycopy_in_heap(arrayOop src_obj, size_t src_offset_in_bytes, T* src_raw,
                                      arrayOop dst_obj, size_t dst_offset_in_bytes, T* dst_raw,
                                      size_t length);

    static void clone_in_heap(oop src, oop dst, size_t size);

    //
    // Not in heap
    //
    template <typename T>
    static oop oop_load_not_in_heap(T* addr);

    template <typename T>
    static oop oop_store_not_in_heap(T* addr, oop new_value);

    template <typename T>
    static oop oop_atomic_cmpxchg_not_in_heap(T* addr, oop compare_value, oop new_value);

    template <typename T>
    static oop oop_atomic_xchg_not_in_heap(T* addr, oop new_value);
  };
};

template<>
struct BarrierSet::GetName<RtgcBarrierSet> {
  static const BarrierSet::Name value = BarrierSet::RtgcBarrierSet;
};

template<>
struct BarrierSet::GetType<BarrierSet::RtgcBarrierSet> {
  typedef ::RtgcBarrierSet type;
};

#endif // SHARE_GC_RTGC_RTGCBARRIERSET_HPP
