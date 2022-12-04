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

#ifndef CPU_X86_GC_RTGC_RTGCBARRIERSETASSEMBLER_X86_HPP
#define CPU_X86_GC_RTGC_RTGCBARRIERSETASSEMBLER_X86_HPP

#include "asm/macroAssembler.hpp"
#include "gc/shared/barrierSetAssembler.hpp"

class RtgcBarrierSetAssembler : public BarrierSetAssembler {
public:
  RtgcBarrierSetAssembler();   

  static void set_args_3(MacroAssembler* masm, Register arg0, Register arg1, Register arg2);

  static void set_args_2(MacroAssembler* masm, Register arg0, Register arg1);

  void oop_replace_at(MacroAssembler* masm, DecoratorSet decorators,
                      Register base, Register addr, Register val, Register tmp1, Register tmp2,
                      Register cmp_v, Register result);
  void oop_replace_at_not_in_heap(MacroAssembler* masm, DecoratorSet decorators,
                      Address dst, Register val, Register tmp1, Register tmp2,
                      Register cmp_v, Register result);

  virtual void oop_load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                      Register dst, Address src, Register tmp1, Register tmp_thread);
  virtual void oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                      Address dst, Register val, Register tmp1, Register tmp2);

  virtual void load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                      Register dst, Address src, Register tmp1, Register tmp_thread);
  virtual void store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                      Address dst, Register val, Register tmp1, Register tmp2);

  virtual void arraycopy_prologue_ex(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                      Register src, Register dst, Register count, 
                      Register dst_array, Label& copy_done, Register saved_count);
};

#endif // CPU_X86_GC_RTGC_MODREFBARRIERSETASSEMBLER_X86_HPP
