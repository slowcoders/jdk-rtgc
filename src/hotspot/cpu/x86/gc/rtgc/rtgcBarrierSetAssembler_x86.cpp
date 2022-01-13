
#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "gc/rtgc/rtgcBarrierSetAssembler_x86.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/rtgc_jrt.hpp"

#define __ masm->

int rtgc_getOopShift();


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
  __ push(rbp);
  __ mov(rbp, rsp);
  __ andptr(rsp, -(StackAlignmentInBytes));

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

  __ mov(rsp, rbp);
  __ pop(rbp);
  if (include_rax) {
    __ pop(rax);
  }
}


void RtgcBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp_thread) {

    BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
}

void RtgcBarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                         Address dst, Register val, Register tmp1, Register tmp2) {
  if (!is_reference_type(type) || (decorators & AS_RAW)) {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    return;
  }

  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  bool is_not_null = (decorators & IS_NOT_NULL) != 0;

  const Register thread = NOT_LP64(rdi) LP64_ONLY(r15_thread); // is callee-saved register (Visual C++ calling conventions)
  Register obj = dst.base();

  push_registers(masm, true, false);

  address fn;
  if (in_heap) {
    if (obj != c_rarg0) {
      __ movptr(c_rarg0, obj);
    }
    __ leaq(c_rarg1, dst);
    if (val != c_rarg2) {
      if (val == noreg) {
        __ xorq(c_rarg2, c_rarg2);
      }
      else {
        __ movptr(c_rarg2, val);
      }
    }

    typedef oopDesc* (*narrow_fn)(oopDesc*, volatile narrowOop*, oopDesc*);
    typedef oopDesc* (*oop_fn)(oopDesc*, volatile oop*, oopDesc*);
    switch (rtgc_getOopShift()) {
      case 0: // fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_0); break;
      case 3: fn = reinterpret_cast<address>((narrow_fn)RtgcBarrier::oop_xchg); break;
      case 8: fn = reinterpret_cast<address>((oop_fn)RtgcBarrier::oop_xchg); break;
      default: assert(false, "invalid oop shift");
    }
  }
  else {
    __ leaq(c_rarg0, dst);
    if (val != c_rarg1) {
      if (val == noreg) {
        __ xorq(c_rarg1, c_rarg1);
      }
      else {
        __ movptr(c_rarg1, val);
      }
    }

    typedef oopDesc* (*narrow_fn)(volatile narrowOop*, oopDesc*);
    typedef oopDesc* (*oop_fn)(volatile oop*, oopDesc*);
    switch (rtgc_getOopShift()) {
      case 0: // fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_0); break;
      case 3: fn = reinterpret_cast<address>((narrow_fn)RtgcBarrier::oop_xchg_in_root); break;
      case 8: fn = reinterpret_cast<address>((oop_fn)RtgcBarrier::oop_xchg_in_root); break;
      default: assert(false, "invalid oop shift");
    }
  }

  __ MacroAssembler::call_VM_leaf_base(fn, in_heap ? 3 : 2);
  pop_registers(masm, true, false);
}


void rtgc_arraycopy_prologue_nocheck(void* src, void* dst, int count) {
  printf("rtgc_arraycopy_prologue_nocheck %p<-%p %d\n", dst, src, count);
}

void RtgcBarrierSetAssembler::arraycopy_prologue(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                                 Register src, Register dst, Register count) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (!is_reference_type(type) || checkcast) return;

  // Call VM
  assert_different_registers(src, dst, count);
  assert(src == c_rarg0, "invalid arg");
  assert(dst == c_rarg1, "invalid arg");
  assert(count == c_rarg2, "invalid arg");

  push_registers(masm, false, false);
  address fn = reinterpret_cast<address>(rtgc_arraycopy_prologue_nocheck);
  __ MacroAssembler::call_VM_leaf_base(fn, 3);
  pop_registers(masm, false, false);

}


void rtgc_arraycopy_epilogue_nocheck(void* src, void* dst, int count) {
  printf("rtgc_arraycopy_epilogue_nocheck %p<-%p %d\n", dst, src, count);
}

void RtgcBarrierSetAssembler::arraycopy_epilogue(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                                   Register src, Register dst, Register count) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;

  if (!is_reference_type(type) || checkcast) return;

  assert(src == c_rarg0, "invalid arg");
  //assert(dst == c_rarg1, "invalid arg");
  //assert(count == c_rarg2, "invalid arg");

  push_registers(masm, false, false);
  address fn = reinterpret_cast<address>(rtgc_arraycopy_epilogue_nocheck);
  __ MacroAssembler::call_VM_leaf_base(fn, 3);
  pop_registers(masm, false, false);
}

int rtgc_arraycopy_hook_checkcast(narrowOop* src, narrowOop* dst, int count, arrayOopDesc* dst_array) {
  printf("rtgc_arraycopy_checkcast %p %p<-%p %d\n", dst_array, dst, src, count);
  return RtgcBarrier::oop_arraycopy_checkcast(src, dst, count, dst_array);
}

bool RtgcBarrierSetAssembler::oop_arraycopy_hook(MacroAssembler* masm, DecoratorSet decorators, Register dst_array,
                                Register src, Register dst, Register count) {
  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;
  if (!checkcast) return false;

  printf("oop_arraycopy_hook checkcast %d, disjoint %d\n", checkcast, disjoint);
  assert(src == c_rarg0, "invalid arg");
  assert(dst == c_rarg1, "invalid arg");
  assert(count == c_rarg2, "invalid arg");
  assert(dst_array == c_rarg3, "invalid arg");

  if (checkcast) {
    address fn;
    typedef int (*narrow_fn)(narrowOop*, narrowOop*, size_t, arrayOopDesc*);
    typedef int (*oop_fn)(oop*, oop*, size_t, arrayOopDesc*);
    switch (rtgc_getOopShift()) {
      case 0: // fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_0); break;
      case 3: fn = reinterpret_cast<address>((narrow_fn)RtgcBarrier::oop_arraycopy_checkcast); break;
      case 8: fn = reinterpret_cast<address>((oop_fn)RtgcBarrier::oop_arraycopy_checkcast); break;
      default: assert(false, "invalid oop shift");
    }

    push_registers(masm, false, false);
    __ MacroAssembler::call_VM_leaf_base(fn, 4);
    pop_registers(masm, false, false);
    return true;
  }
  else {
    //__ MacroAssembler::call_VM_leaf_base(reinterpret_cast<address>(rtgc_arraycopy_nocheck), 4);
    return false;
  }
}


#endif