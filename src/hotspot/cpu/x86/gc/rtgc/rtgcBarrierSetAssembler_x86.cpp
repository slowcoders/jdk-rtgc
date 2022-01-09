
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
  if (!is_reference_type(type) || (decorators & AS_RAW)) {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    return;
  }

  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  bool is_not_null = (decorators & IS_NOT_NULL) != 0;
  bool atomic = (decorators & MO_RELAXED) != 0;

  bool isArray = dst.scale() != Address::times_1;
  // if (!isArray) {
  //   assert(dst.disp() == 0, "must be");
  //   if (dst.index() == 0) {
  //     printf("set klass\n");
  //     __ store_heap_oop(dst, val, rdx, rbx, decorators);
  //     return;
  //   }
  // }

  const Register thread = NOT_LP64(rdi) LP64_ONLY(r15_thread); // is callee-saved register (Visual C++ calling conventions)
  Register obj = dst.base();
  Register off = dst.index();
  const bool do_save_Java_frame = false;

  // if (ENABLE_RTGC_STORE_TEST) {
  //   __ push(obj);
  //   __ store_heap_oop(dst, val, rdx, rbx, decorators);
  //   __ pop(obj);
  // }

  if (do_save_Java_frame) {
    address the_pc = __ pc();
    // int call_offset = __ offset();
    __ set_last_Java_frame(thread, noreg, rbp, the_pc);
  }

  push_registers(masm, true, false);
  // __ pusha();  

  if (obj != c_rarg0) {
    __ movptr(c_rarg0, obj);
  }
  __ leaq(c_rarg1, dst);
  // if (off != rsi) {
  //   __ mov(rsi, off);
  // }
  if (val != c_rarg2) {
    if (val == noreg) {
      __ xorq(c_rarg2, c_rarg2);
    }
    else {
      __ movptr(c_rarg2, val);
    }
  }
  // __ xorq(rcx, rcx);

  address fn;
  // '' = isArray
  //     ? CAST_FROM_FN_PTR(address, RTGC::RTGC_StoreObjArrayItem)
  //     : CAST_FROM_FN_PTR(address, RTGC::RTGC_StoreObjField);
  typedef oopDesc* (*narrow_fn)(oopDesc*, volatile narrowOop*, oopDesc*);
  typedef oopDesc* (*oop_fn)(oopDesc*, volatile oop*, oopDesc*);

  switch (rtgc_getOopShift()) {
    case 0: // fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_0); break;
    case 3: fn = reinterpret_cast<address>((narrow_fn)RtgcBarrier::oop_xchg); break;
    case 8: fn = reinterpret_cast<address>((oop_fn)RtgcBarrier::oop_xchg); break;
    default: assert(false, "invalid oop shift");
  }

  __ MacroAssembler::call_VM_leaf_base(fn, 3);
  // __ call(RuntimeAddress(fn));

  if (do_save_Java_frame) {
    __ reset_last_Java_frame(thread, true);
  }
  pop_registers(masm, true, false);
}


int RtgcBarrierSetAssembler::arraycopy_checkcast(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                Register src, Register dst, Register count,
                                Register dst_array) {
  assert((decorators & ARRAYCOPY_CHECKCAST) != 0, "no arraycopy_checkcast");
  assert((decorators & ARRAYCOPY_DISJOINT) != 0, "no arraycopy_checkcast");
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;

  push_registers(masm, false, false);
  __ MacroAssembler::call_VM_leaf_base(reinterpret_cast<address>(rtgc_arraycopy_checkcast), 3);
  pop_registers(masm, false, false);
  return +1;
}


#endif