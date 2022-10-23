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

#include "precompiled.hpp"
#include "runtime/thread.hpp"
#include "gc/rtgc/rtgcBarrierSet.hpp"
#include "gc/rtgc/rtgcBarrierSetAssembler.hpp"
#include "utilities/macros.hpp"
#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/rtgcDebug.hpp"

RtgcBarrierSet::RtgcBarrierSet(CardTable* card_table) :  
    ModRefBarrierSet(make_barrier_set_assembler<RtgcBarrierSetAssembler>(),
                     make_barrier_set_c1<RtgcBarrierSetC1>(),
                     make_barrier_set_c2<BarrierSetC2>(),
                     BarrierSet::FakeRtti(BarrierSet::RtgcBarrierSet)), 
    _card_table(card_table) {
  initialize();
};

void RtgcBarrierSet::initialize() {
  RtgcBarrier::init_barrier_runtime();
}

void RtgcBarrierSet::on_thread_create(Thread *thread) {
}

void RtgcBarrierSet::on_thread_destroy(Thread *thread) {
}

void RtgcBarrierSet::on_slowpath_allocation_exit(JavaThread* thread, oop new_obj) {
#ifdef ASSERT  
  // ** Do not mark empty_trackable. It will marked later as promoted trackable.
  if (!_card_table->is_in_young(new_obj)) {
    rtgc_log(true, "on_slowpath_allocation_exit trackable %p\n", (void*)new_obj);
    //rtHeap::mark_empty_trackable(new_obj);
  }
#endif
}