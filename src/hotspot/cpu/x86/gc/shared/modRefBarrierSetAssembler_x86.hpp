/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_X86_GC_SHARED_MODREFBARRIERSETASSEMBLER_X86_HPP
#define CPU_X86_GC_SHARED_MODREFBARRIERSETASSEMBLER_X86_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/gc_globals.hpp"

// The ModRefBarrierSetAssembler filters away accesses on BasicTypes other
// than T_OBJECT/T_ARRAY (oops). The oop accesses call one of the protected
// accesses, which are overridden in the concrete BarrierSetAssembler.

#if INCLUDE_RTGC // INCLUDE RTGC Barrier
#include "gc/rtgc/rtgcBarrierSetAssembler.hpp"

class ModRefBarrierSetAssembler: public RtgcBarrierSetAssembler {
#else
class ModRefBarrierSetAssembler: public BarrierSetAssembler {
#endif
protected:
  virtual void gen_write_ref_array_pre_barrier(MacroAssembler* masm, DecoratorSet decorators,
                                               Register addr, Register count) {}
  virtual void gen_write_ref_array_post_barrier(MacroAssembler* masm, DecoratorSet decorators,
                                                Register addr, Register count, Register tmp) {}
  virtual void oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                            Address dst, Register val, Register tmp1, Register tmp2) = 0;
public:

#if INCLUDE_RTGC // ENABLE_ARRAY_COPY_HOOK
  virtual void arraycopy_prologue_ex(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                  Register src, Register dst, Register count, 
                                  Register dst_array, Label& copy_done, Register saved_count = noreg) {
    arraycopy_prologue(masm, decorators, type, src, dst, count); 
  }
#endif

  virtual void arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                  Register src, Register dst, Register count);
  virtual void arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                  Register src, Register dst, Register count);

  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                        Address dst, Register val, Register tmp1, Register tmp2);
};

#endif // CPU_X86_GC_SHARED_MODREFBARRIERSETASSEMBLER_X86_HPP
