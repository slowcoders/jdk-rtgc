
#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "gc/rtgc/rtgcBarrierSetAssembler.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/rtgc_jrt.hpp"

#define __ masm->

static const DecoratorSet AS_NO_REF = AS_RAW | AS_NO_KEEPALIVE;

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

RtgcBarrierSetAssembler::RtgcBarrierSetAssembler() {
  RtgcBarrier::init_barrier_runtime();
}

void RtgcBarrierSetAssembler::oop_load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp_thread) {
  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
}

void RtgcBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp_thread) {
  if (is_reference_type(type) && !(decorators & AS_NO_REF)) {  
    oop_load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  }
  else {
    BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  }
}

void RtgcBarrierSetAssembler::oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                         Address dst, Register val, Register tmp1, Register tmp2) {
  if (!is_reference_type(type) || (decorators & AS_NO_REF)) {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    return;
  }

  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  bool is_not_null = (decorators & IS_NOT_NULL) != 0;

  //const Register thread = NOT_LP64(rdi) LP64_ONLY(r15_thread); // is callee-saved register (Visual C++ calling conventions)
  Register obj = dst.base();

  push_registers(masm, true, false);

  assert_different_registers(c_rarg0, val);
  if (in_heap) {
    assert(dst.index() != noreg || dst.disp() != 0, "absent dst object pointer");
    assert_different_registers(c_rarg2, val);
    if (dst.index() == c_rarg2) {
      __ leaq(c_rarg0, dst);
      __ movptr(c_rarg2, obj);
    } else {
      if (obj != c_rarg2) {
        assert_different_registers(c_rarg2, val);
        __ movptr(c_rarg2, obj);
      }
      __ leaq(c_rarg0, dst);
    }
  }
  else if (dst.index() != noreg || dst.disp() != 0) {
    __ lea(c_rarg0, dst);  
  } else if (dst.base() != c_rarg0) {
    __ movptr(c_rarg0, dst.base());
  }

  if (val != c_rarg1) {
    if (val == noreg) {
      __ xorq(c_rarg1, c_rarg1);
    } else {
      __ movptr(c_rarg1, val);
    }
  }

  address fn = RtgcBarrier::getStoreFunction(in_heap);
  __ MacroAssembler::call_VM_leaf_base(fn, in_heap ? 3 : 2);
  pop_registers(masm, true, false);
}

void RtgcBarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                         Address dst, Register val, Register tmp1, Register tmp2) {
  if (is_reference_type(type)) {
    oop_store_at(masm, decorators, type, dst, val, tmp1, tmp2);
  } else {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
  }
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

  printf("oop_arraycopy_hook checkcast %d, disjoint %d\n", checkcast, disjoint);
  assert(src == c_rarg0, "invalid arg");
  assert(dst == c_rarg1, "invalid arg");
  assert(count == c_rarg2, "invalid arg");
  assert(dst_array == c_rarg3, "invalid arg");

  address fn = RtgcBarrier::getArrayCopyFunction(decorators);
  if (checkcast) {
    push_registers(masm, false, false);
    __ MacroAssembler::call_VM_leaf_base(fn, 4);
    pop_registers(masm, false, false);
    return checkcast;
  }
  return false;
}

