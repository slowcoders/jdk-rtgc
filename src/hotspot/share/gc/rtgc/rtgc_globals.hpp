/*
 * Copyright (c) 2017, 2018, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_GC_RTGC_RTGC_GLOBALS_HPP
#define SHARE_GC_RTGC_RTGC_GLOBALS_HPP

#include "runtime/globals_shared.hpp"

//
// Defines all globals flags used by the Rtgc GC.
//

//
// Defines all globals flags used by the Rtgc GC.
//

#define GC_RTGC_FLAGS(develop,                                           \
                         develop_pd,                                        \
                         product,                                           \
                         product_pd,                                        \
                         notproduct,                                        \
                         range,                                             \
                         constraint)                                        \
                                                                            \
  product(size_t, RtgcPrintHeapSteps, 20, EXPERIMENTAL,                  \
          "Print heap occupancy stats with this number of steps. "          \
          "0 turns the printing off.")                                      \
          range(0, max_intx)                                                \
                                                                            \
  product(size_t, RtgcUpdateCountersStep, 1 * M, EXPERIMENTAL,           \
          "Update heap occupancy counters after allocating this much "      \
          "memory. Higher values would make allocations faster at "         \
          "the expense of lower resolution in heap counters.")              \
          range(1, max_intx)                                                \
                                                                            \
  product(size_t, RtgcMaxTLABSize, 4 * M, EXPERIMENTAL,                  \
          "Max TLAB size to use with Rtgc GC. Larger value improves "    \
          "performance at the expense of per-thread memory waste. This "    \
          "asks TLAB machinery to cap TLAB sizes at this value.")           \
          range(1, max_intx)                                                \
                                                                            \
  product(bool, RtgcElasticTLAB, true, EXPERIMENTAL,                     \
          "Use elastic policy to manage TLAB sizes. This conserves memory " \
          "for non-actively allocating threads, even when they request "    \
          "large TLABs for themselves. Active threads would experience "    \
          "smaller TLABs until policy catches up.")                         \
                                                                            \
  product(bool, RtgcElasticTLABDecay, true, EXPERIMENTAL,                \
          "Use timed decays to shrink TLAB sizes. This conserves memory "   \
          "for the threads that allocate in bursts of different sizes, "    \
          "for example the small/rare allocations coming after the initial "\
          "large burst.")                                                   \
                                                                            \
  product(double, RtgcTLABElasticity, 1.1, EXPERIMENTAL,                 \
          "Multiplier to use when deciding on next TLAB size. Larger value "\
          "improves performance at the expense of per-thread memory waste. "\
          "Lower value improves memory footprint, but penalizes actively "  \
          "allocating threads.")                                            \
          range(1.0, DBL_MAX)                                               \
                                                                            \
  product(size_t, RtgcTLABDecayTime, 1000, EXPERIMENTAL,                 \
          "TLAB sizing policy decays to initial size after thread had not " \
          "allocated for this long. Time is in milliseconds. Lower value "  \
          "improves memory footprint, but penalizes actively allocating "   \
          "threads.")                                                       \
          range(1, max_intx)                                                \
                                                                            \
  product(size_t, RtgcMinHeapExpand, 128 * M, EXPERIMENTAL,              \
          "Min expansion step for heap. Larger value improves performance " \
          "at the potential expense of memory waste.")                      \
          range(1, max_intx)

#endif // SHARE_GC_RTGC_RTGC_GLOBALS_HPP
