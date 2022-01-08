
#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "gc/rtgc/rtgcBarrierSetAssembler_x86.hpp"

#define __ masm->



#if 1


/*
MS X64
  Args:       RCX/XMM0, RDX/XMM1, R8/XMM2, R9/XMM3
  Volatile registers : RAX, RCX, RDX, R8, R9, R10, R11 (caller-saved).
  Non volatile: RBX, RBP, RDI, RSI, RSP, R12-R15 (callee-saved).

System V AMD64 (Linux)
  Args:       RDI, RSI, RDX, RCX, R8, R9, [XYZ]MM0–7
  Volatile registers : RAX, RCX, RDX, RDI, RSI, R8, R9, R10, R11 (caller-saved).
  Non volatile: RBX, RSP, RBP, and R12–R15, (callee-saved)
*/

void push_registers(MacroAssembler* masm, bool include_rax, bool include_r12_r15) {
  if (include_rax) {
    __ push(rax);
  }
  __ push(rcx);
  __ push(rdx);
  __ push(rdi);
  __ push(rsi);
#ifdef _LP64
  __ push(r8);
  __ push(r9);
  __ push(r10);
  __ push(r11);
  if (include_r12_r15) {
    __ push(r12);
    __ push(r13);
    __ push(r14);
    __ push(r15);
  }

#endif
}

void pop_registers(MacroAssembler* masm, bool include_rax, bool include_r12_r15) {
#ifdef _LP64
  if (include_r12_r15) {
    __ pop(r15);
    __ pop(r14);
    __ pop(r13);
    __ pop(r12);
  }
  __ pop(r11);
  __ pop(r10);
  __ pop(r9);
  __ pop(r8);
#endif
  __ pop(rsi);
  __ pop(rdi);
  __ pop(rdx);
  __ pop(rcx);

  if (include_rax) {
    __ pop(rax);
  }
}

void rtgc_arraycopy_prologue(void* src, void* dst, int count, arrayOopDesc* array) {
  printf("rtgc_arraycopy_prologue %p %p<-%p %d\n", array, dst, src, count);
}

void rtgc_arraycopy_epilogue(void* src, void* dst, int count, arrayOopDesc* array) {
  printf("rtgc_arraycopy_epilogue %p %p<-%p %d\n", array, dst, src, count);
}

void rtgc_arraycopy_checkcast(void* src, void* dst, int count, arrayOopDesc* array) {
  printf("rtgc_arraycopy_checkcast %p %p<-%p %d\n", array, dst, src, count);
}

void RtgcBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                                 Register src, Register dst, Register count,
                                                 Register rtgc_dst_array) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (!is_reference_type(type)) return;

  // Call VM
  assert_different_registers(src, dst, count, rtgc_dst_array);
  assert(src == c_rarg0, "invalid arg");
  assert(dst == c_rarg1, "invalid arg");
  assert(count == c_rarg2, "invalid arg");
  assert(c_rarg3 == rtgc_dst_array, "invalid arg");

  push_registers(masm, true, false);
  __ MacroAssembler::call_VM_leaf_base(reinterpret_cast<address>(rtgc_arraycopy_prologue), 4);
  pop_registers(masm, true, false);

}

void RtgcBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                                   Register src, Register dst, Register count) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;

  if (!is_reference_type(type)) return;

  assert(src == c_rarg0, "invalid arg");
  //assert(dst == c_rarg1, "invalid arg");
  //assert(count == c_rarg2, "invalid arg");

  push_registers(masm, true, false);
  __ MacroAssembler::call_VM_leaf_base(reinterpret_cast<address>(rtgc_arraycopy_epilogue), 3);
  pop_registers(masm, true, false);
}


void RtgcBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp_thread) {

    BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
}

void RtgcBarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                         Address dst, Register val, Register tmp1, Register tmp2) {
  if (false && is_reference_type(type) && !(decorators & AS_RAW)) {
    //oop_store_at(masm, decorators, type, dst, val, tmp1, tmp2);
  } else {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
  }
}


int RtgcBarrierSetAssembler::arraycopy_checkcast(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                Register src, Register dst, Register count,
                                Register dst_array) {
  assert((decorators & ARRAYCOPY_CHECKCAST) != 0, "no arraycopy_checkcast");
  assert((decorators & ARRAYCOPY_DISJOINT) != 0, "no arraycopy_checkcast");
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;

  push_registers(masm, false, false);
  __ MacroAssembler::call_VM_leaf_base(reinterpret_cast<address>(rtgc_arraycopy_checkcast), 3);
  push_registers(masm, false, false);
  return +1;
}


#endif