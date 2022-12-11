
#include "precompiled.hpp"
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "gc/rtgc/rtgcBarrierSetAssembler.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/rtgcGlobals.hpp"
#include "gc/rtgc/impl/GCNode.hpp"
#include "gc/rtgc/rtHeapEx.hpp"
#include "gc/rtgc/rtThreadLocalData.hpp"

using namespace RTGC;

#define __ masm->

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
  // __ push(rbp);
  // __ mov(rbp, rsp);
  // __ andptr(rsp, -(StackAlignmentInBytes));

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

  // __ mov(rsp, rbp);
  // __ pop(rbp);
  if (include_rax) {
    __ pop(rax);
  }
}

RtgcBarrierSetAssembler::RtgcBarrierSetAssembler() {
  RtgcBarrier::init_barrier_runtime();
}

static bool __needBarrier(BasicType type, DecoratorSet decorators, Address dst, bool op_store) {
  if (!is_reference_type(type) || RtgcBarrier::is_raw_access(decorators, op_store)) return false;
  return dst.index() != noreg || dst.disp() > oopDesc::klass_offset_in_bytes();
}

void RtgcBarrierSetAssembler::oop_load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp_thread) {
  BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  if (__needBarrier(type, decorators, src, false)) {
    fatal("parallel gc is not implmented.");
  }
}

void RtgcBarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                       Register dst, Address src, Register tmp1, Register tmp_thread) {
  if (is_reference_type(type)) {
    BarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  }
  else {
    oop_load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  }
}

static void __checkTrackable(MacroAssembler* masm, Register base, Label& rawAccess, Register tmp3) {
  if (!USE_EXPLICIT_TRACKABLE_MARK) {
    const Register thread = NOT_LP64(rdi) LP64_ONLY(r15_thread); // is callee-saved register (Visual C++ calling conventions)
    Address trackable_start(thread, RtThreadLocalData::trackable_heap_start_offset());
    __ cmpptr(base, trackable_start);
    __ jcc(Assembler::less, rawAccess);
  } else {
    ByteSize offset_gc_flags = in_ByteSize(offset_of(RTGC::GCNode, _flags));
    __ movl(tmp3, Address(base, offset_gc_flags));
    __ testl(tmp3, (int32_t)RTGC::TRACKABLE_BIT);
    __ jcc(Assembler::zero, rawAccess);
  }
}

static int cnt_log = 0;
static void __trace_update_log(oopDesc* anchor, ErasedSlot erasedField, void* rtData) {  
  printf("trace update log %p[%d] v= %x rtData=%p thread=%p\n", 
    anchor, erasedField._offset, (int32_t)erasedField._obj, rtData, Thread::current());
  if (to_obj(anchor)->isTrackable() && !rtHeap::is_modified(erasedField._obj)) {
    RtThreadLocalData::addUpdateLog(anchor, erasedField, RtThreadLocalData::data(Thread::current()));
  }
}

static void __check_update_log(oopDesc* anchor, volatile narrowOop* field, narrowOop erased, RtThreadLocalData* rtData) {
  // printf("check log %p[%p] v= %x rtData=%p thread=%p\n", anchor, field, (int32_t)erased, rtData, Thread::current());
  rtData->checkLastLog(anchor, field, erased);
  postcond(cnt_log < 100);
}


#include "c1/c1_Decorators.hpp"
void RtgcBarrierSetAssembler::oop_replace_at(MacroAssembler* masm, DecoratorSet decorators,
                                            Register base, Register addr, Register val, 
                                            ReplaceType type, Register cmp_v) {
  precond((decorators & IN_HEAP) != 0);
  precond((decorators & IS_DEST_UNINITIALIZED) == 0);
  precond((decorators & (AS_RAW | AS_NO_KEEPALIVE)) == 0);
  precond((decorators & ON_PHANTOM_OOP_REF) == 0);
  precond(val != noreg);

  bool dbg_trace = 0;//(decorators & C1_NEEDS_PATCHING) != 0;
  bool check_log = 0;//(decorators & C1_NEEDS_PATCHING) != 0;
  // decorators &= ~C1_NEEDS_PATCHING;

  bool in_native = (decorators & IN_NATIVE) != 0;
  bool is_not_null = (decorators & IS_NOT_NULL) != 0;
  
  Label L_raw_access, L_done, L_add_modify_log, L_cmpxchg_2nd, L_cmpxchg_success, L_cmpxchg_fail;
  Label L_decode_xchg_result, L_no_modify_access, L_slowAccess;
  const Register thread = NOT_LP64(rdi) LP64_ONLY(r15_thread); // is callee-saved register (Visual C++ calling conventions)
  const int32_t modified_null = 1;
  assert_different_registers(val, base, addr, thread, rscratch1, cmp_v);

  if (is_not_null) {
    __ encode_heap_oop_not_null(val);
  } else {
    __ encode_heap_oop(val);
  }
  if (type == ReplaceType::CmpXchg) {
    __ encode_heap_oop(cmp_v);
  }

  if (!dbg_trace) {
    __checkTrackable(masm, base, L_raw_access, rscratch1);
  } else {
    __checkTrackable(masm, base, L_no_modify_access, rscratch1);
  }

  // set modified-bit; 
  __ orl(val, 1); 

  __ bind(L_no_modify_access);
  if (type == ReplaceType::CmpXchg) {
    __ movl(rscratch1, cmp_v);
    __ lock();
    __ cmpxchgl(val, Address(addr, 0)); // cmp_v = rax
    __ jcc(Assembler::notEqual, L_cmpxchg_2nd); 
  } else {
    // xchg 는 lock prefix 불필요.
    __ xchgl(val, Address(addr, 0));

    if (!dbg_trace) {
      __ testl(val, 1);
      if (type == ReplaceType::Xchg) {
        __ jcc(Assembler::notZero, L_decode_xchg_result);
      } else {
        __ jcc(Assembler::notZero, L_done);
      }
    }
  }

  __ bind(L_add_modify_log); {
    if (type == ReplaceType::CmpXchg) {
      __ movl(val, cmp_v);
    }
    __ shlq(val, 32);
    __ subptr(addr, base);
    __ addptr(val, addr); 

    const Register log = addr;

    __ movptr(rscratch1, Address(thread, RtThreadLocalData::log_sp_offset()));
    __ movptr(log, Address(rscratch1, 0));
    __ subptr(log, sizeof(FieldUpdateLog));
    if (!dbg_trace) {
      __ cmpptr(log, rscratch1);
      __ jcc(Assembler::lessEqual, L_slowAccess);
      __ movptr(Address(rscratch1, 0), log);
      __ movptr(Address(log, ByteSize(0)), base);
      __ movq(Address(log, ByteSize(8)), val);
      
      if (type == ReplaceType::Xchg) {
        __ shrq(val, 32);
        __ jmp(L_decode_xchg_result);
      } else if (type == ReplaceType::CmpXchg) {
        __ jmp(L_cmpxchg_success);
      } else {
        __ jmp(L_done);
      }
    }
  }

  // =====================
  __ bind(L_slowAccess); {
    push_registers(masm, true, false);
    set_args_2(masm, base, val);
    __ leaq(c_rarg2, Address(thread, Thread::gc_data_offset()));

    address fn;
    if (dbg_trace) {
      fn = (address)__trace_update_log;
    } else if (check_log) {
      fn = (address)__check_update_log;
    } else {
      fn = (address)RtThreadLocalData::addUpdateLog;
    }
    __ MacroAssembler::call_VM_leaf_base(fn, 3);
    pop_registers(masm, true, false);

    if (type == ReplaceType::Xchg) {
      __ shrq(val, 32);
      __ jmp(L_decode_xchg_result);
    } else if (type == ReplaceType::CmpXchg) {
      __ jmp(L_cmpxchg_success);
    } else {
      __ jmp(L_done);
    }
  }


  //====================
  __ bind(L_raw_access); {
    Address dst = Address(addr, 0);
    switch (type) {
      case ReplaceType::Store:
        // --> BarrierSetAssembler::store_at(masm, decorators, T_OBJECT, dst, val_org, noreg, noreg);
        __ movl(dst, val);
        break;
      case ReplaceType::Xchg:
        __ xchgl(val, dst);
        __ bind(L_decode_xchg_result); {
          __ decode_heap_oop(val);
        }
        break;
      case ReplaceType::CmpXchg:
        __ movl(rscratch1, cmp_v);
        __ lock();
        __ cmpxchgl(val, Address(addr, 0));
        __ jcc(Assembler::equal, L_cmpxchg_success);

        // clone 하거나, array item 을 young array 로 복사하는 경우, modified-bit 가 clear 되지 않는다.
        __ bind(L_cmpxchg_2nd); {
          __ orl(rscratch1, 1); // set modified bit
          __ cmpl(cmp_v, rscratch1); // *addr == (new_v | modified)
          __ jcc(Assembler::notEqual, L_cmpxchg_fail);

          __ movl(cmp_v, rscratch1);
          __ cmpxchgl(val, Address(addr, 0));
          __ jcc(Assembler::equal, L_cmpxchg_success);
        }
        __ bind(L_cmpxchg_fail); {
          __ xorl(val, val);
          __ jmp(L_done);
        }
        __ bind(L_cmpxchg_success); {
          __ movl(val, 1);
        }
        break;
    }
  }  
  __ bind(L_done);
  //====================
}

void RtgcBarrierSetAssembler::oop_replace_at_not_in_heap(MacroAssembler* masm, DecoratorSet decorators,
                                            Address dst, Register val, ReplaceType type,
                                            Register cmp_v) {
  precond((decorators & IN_HEAP) == 0);

  push_registers(masm, true, false);

  if (val == c_rarg0) {
    __ movptr(rscratch1, val);
    val = rscratch1;
  } 
  
  if (dst.index() != noreg || dst.disp() != 0) {
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

  address fn = (address)RtgcBarrier::getStoreFunction(decorators);
  __ MacroAssembler::call_VM_leaf_base(fn, 2);
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

void RtgcBarrierSetAssembler::arraycopy_prologue_ex(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                  Register src, Register dst, Register count, 
                                  Register dst_array, Label& copy_done, Register saved_count) {
  this->arraycopy_prologue(masm, decorators, type, src, dst, count); 
  if (type != T_OBJECT) return;

  bool checkcast = (decorators & ARRAYCOPY_CHECKCAST) != 0;
  bool disjoint = (decorators & ARRAYCOPY_DISJOINT) != 0;
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;

  // printf("oop_arraycopy_hook checkcast %d, disjoint %d\n", checkcast, disjoint);
  assert(src == c_rarg0, "invalid arg");
  assert(dst == c_rarg1, "invalid arg");
  assert(count == c_rarg2, "invalid arg");
  assert(dst_array == c_rarg3, "invalid arg");

  address fn = RtgcBarrier::getArrayCopyFunction(decorators);
  Label L_raw_access;

  __checkTrackable(masm, dst_array, L_raw_access, rax);

  push_registers(masm, false, false);
  __ MacroAssembler::call_VM_leaf_base(fn, 4);
  pop_registers(masm, false, false);
  if (saved_count != noreg) {
    __ movptr(saved_count, count);
  }
  __ jmp(copy_done);
  __ bind(L_raw_access);
  return;
}

void RtgcBarrierSetAssembler::oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                         Address dst, Register val, Register tmp1, Register tmp2) {
  if (!__needBarrier(type, decorators, dst, true)) {
    BarrierSetAssembler::store_at(masm, decorators, type, dst, val, tmp1, tmp2);
    return;
  } 
  
  bool in_heap = (decorators & IN_HEAP) != 0;
  if (rtHeap::useModifyFlag()) {
    if (in_heap) {
      const Register addr = LP64_ONLY(r8) NOT_LP64(rsi);
      const Register base = dst.base();
      assert(!dst.uses(val), "not enough registers");
      assert(!dst.uses(addr), "not enough registers");

      precond(base != addr);
      precond(val != addr);
      __ leaq(addr, dst);
      if (val == noreg) {
        val = rscratch2;
        __ movptr(val, 0);
      }
      oop_replace_at(masm, decorators, base, addr, val, ReplaceType::Store, noreg);
      return;
    } 
    // else {
    //   oop_replace_at_not_in_heap(masm, decorators, dst, val, tmp1, tmp2, noreg, noreg);
    // }
    // return;
  }

  
  // ================== //
  Register base = dst.base();

  Label L_raw_access, L_done;

  if (in_heap) {
    Register tmp3 = LP64_ONLY(r8) NOT_LP64(rsi);
    __checkTrackable(masm, base, L_raw_access, tmp3);

    push_registers(masm, true, false);

    assert_different_registers(c_rarg0, val);
    assert(dst.index() != noreg || dst.disp() != 0, "absent dst object pointer");
    assert_different_registers(c_rarg2, val);
    if (dst.index() == c_rarg2) {
      __ leaq(c_rarg0, dst);
      __ movptr(c_rarg2, base);
    } else {
      if (base != c_rarg2) {
        assert_different_registers(c_rarg2, val);
        __ movptr(c_rarg2, base);
      }
      __ leaq(c_rarg0, dst);
    }
  }
  else {
    push_registers(masm, true, false);

    if (dst.index() != noreg || dst.disp() != 0) {
      __ lea(c_rarg0, dst);  
    } else if (dst.base() != c_rarg0) {
      __ movptr(c_rarg0, dst.base());
    }
  }

  if (val != c_rarg1) {
    if (val == noreg) {
      __ xorq(c_rarg1, c_rarg1);
    } else {
      __ movptr(c_rarg1, val);
    }
  }

  address fn = RtgcBarrier::getStoreFunction(decorators);
  __ MacroAssembler::call_VM_leaf_base(fn, in_heap ? 3 : 2);
  pop_registers(masm, true, false);
  __ jmp(L_done);
  __ bind(L_raw_access);
  BarrierSetAssembler::store_at(masm, decorators, type, dst, val, noreg, noreg);
  __ bind(L_done);
}

void RtgcBarrierSetAssembler::set_args_2(MacroAssembler* masm, Register a0, Register a1) {
  if (a0 != c_rarg0) {
    if (a1 == c_rarg0) {
      __ xchgptr(a1, a0);
      a1 = a0;
    } else {
      __ movptr(c_rarg0, a0);
    }
  }
  if (a1 != c_rarg1) {
    __ movptr(c_rarg1, a1);
  }
}

void RtgcBarrierSetAssembler::set_args_3(MacroAssembler* masm, Register a0, Register a1, Register a2) {
  if (a0 != c_rarg0) {
    if (a1 == c_rarg0) {
      __ xchgptr(a1, a0);
      a1 = a0;
    } else if (a2 == c_rarg0) {
      __ xchgptr(a2, a0);
      a2 = a0;
    } else {
      __ movptr(c_rarg0, a0);
    }
  }
  if (a1 != c_rarg1) {
    if (a2 == c_rarg1) {
      __ xchgptr(a2, a1);
      a2 = a1;
    } else {
      __ movptr(c_rarg1, a1);
    }
  }
  if (a2 != c_rarg2) {
     __ movptr(c_rarg2, a2);
  }
}