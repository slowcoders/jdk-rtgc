
#include "precompiled.hpp"
#include "c1/c1_Compilation.hpp"
#include "c1/c1_Defs.hpp"
#include "c1/c1_FrameMap.hpp"
#include "c1/c1_Instruction.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_LIRGenerator.hpp"
#include "c1/c1_ValueStack.hpp"
#include "ci/ciArrayKlass.hpp"
#include "ci/ciInstance.hpp"
#include "ci/ciObjArray.hpp"
#include "ci/ciUtilities.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"
#include "oops/klass.inline.hpp"
#include "classfile/vmClasses.hpp"
#include "classfile/vmSymbols.hpp"
#include "runtime/arguments.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vm_version.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/macros.hpp"
#include "utilities/sizes.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "asm/assembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#ifdef COMPILER1
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/z/c1/zBarrierSetC1.hpp"
#endif // COMPILER1

#include "gc/shared/barrierSetAssembler_x86.hpp"
#include "gc/rtgc/rtgcGlobals.hpp"
#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/impl/GCObject.hpp"
#include "gc/rtgc/rtHeapEx.hpp"
#include "gc/rtgc/rtgcBarrierSetAssembler.hpp"

static int rtgc_log_trigger = 0;
static const bool ENABLE_CPU_MEMBAR = false;
#define ill   LIR_OprFact::illegalOpr
#define __    gen->lir()-> 

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_BARRIER_C1, function);
}

void __trace(narrowOop* addr) {
  //rtgc_log(1, "__TRCAE %p\n", addr);
}
void __break(narrowOop* addr) {
  assert(false, "__BREAK %p\n", addr);
}

class LIR_OpCompareAndSwapOop : public LIR_OpCompareAndSwap {
  LIR_Opr _base;
public:
  LIR_OpCompareAndSwapOop(LIR_Opr base, LIR_Opr addr, LIR_Opr cmp_value, LIR_Opr new_value,
      LIR_Opr t1, LIR_Opr t2, LIR_Opr result) 
      : LIR_OpCompareAndSwap(lir_cas_obj, addr, cmp_value, new_value, t2, t2, result) {
        this->_base = base;
  }

  virtual void emit_code(LIR_Assembler* masm);
  virtual void visit(LIR_OpVisitState* state);
};

void LIR_OpCompareAndSwapOop::visit(LIR_OpVisitState* state) {
  // _code 가 lir_none 인 경우에만 호출된다. 
  // (LIR_OpCompareAndSwap 을 상속한 경우엔 호출되지 않는다)
  fatal("should not reach here!");
  // assert(addr()->is_valid(),      "used");
  // assert(cmp_value()->is_valid(), "used");
  // assert(new_value()->is_valid(), "used");
  // if (info())               state->do_info(info());
  //                           state->do_input(addr());
  //                           state->do_temp(addr());
  //                           state->do_input(cmp_value());
  //                           state->do_temp(cmp_value());
  //                           state->do_input(new_value());
  //                           state->do_temp(new_value());
  // if (tmp1->is_valid())     state->do_temp(tmp1());
  // if (tmp2->is_valid())     state->do_temp(tmp2());
  // if (result()->is_valid()) state->do_output(result());
}

void addUpdateLog(LIR_Assembler* masm, Register base, Register addr, Register erased) {
  Label L_done;
  C1_MacroAssembler* cm = masm->masm();
  
  // check dirty
  cm-> testl(erased, 1);
  
  /*if need log upate log*/
  cm-> jcc(Assembler::Condition::zero, L_done);
  {
    const Register thread = NOT_LP64(rdi) LP64_ONLY(r15_thread); // is callee-saved register (Visual C++ calling conventions)
    Address log_top(thread, Thread::gc_data_offset());
    cm-> movq(rscratch1, -16);

    cm-> lock();
    cm-> xaddq(log_top, rscratch1);

    Address new_log(rscratch1, 0);
    cm-> movq(new_log, base);
    cm-> addq(rscratch1, 8);
    cm-> subl(addr, base);
    cm-> movl(new_log, addr);
    cm-> addq(rscratch1, 4);
    cm-> movl(new_log, erased);
  }
  cm-> bind(L_done);
}

void LIR_OpCompareAndSwapOop::emit_code(LIR_Assembler* masm) {
  
  NOT_LP64(assert(this->addr()->is_single_cpu(), "must be single");)
  Register base = this->_base->as_register();
  Register addr = (this->addr()->is_single_cpu() ? this->addr()->as_register() : this->addr()->as_register_lo());
  Register newval = this->new_value()->as_register();
  Register cmpval = this->cmp_value()->as_register();
  assert(cmpval == rax, "wrong register");
  assert(newval != NULL, "new val must be register");
  assert(cmpval != newval, "cmp and new values must be in different registers");
  assert(cmpval != addr, "cmp and addr must be in different registers");
  assert(newval != addr, "new value and addr must be in different registers");
  C1_MacroAssembler* cm = masm->masm();
  cm->call(RuntimeAddress((address)__trace));

#ifdef _LP64
  if (UseCompressedOops) {
    Label L_compare_fast;
    // @zee) modify-flag 사용 시 2회 검사 필요!!!
    Label L_compare_done;
    Label L_compare_dirty;
    Label L_compare_first_fail;

    cm-> encode_heap_oop(cmpval);

    cm-> movl(rscratch1, Address(addr, 0));
    cm-> testl(rscratch1, 1);
    cm-> jcc(Assembler::Condition::zero, L_compare_dirty);

    cm-> movl(rscratch1, cmpval);
    cm-> shlq(cmpval, 32);
    cm-> addq(cmpval, rscratch1);
    cm-> andl(cmpval, ~1);
    
    cm-> mov(rscratch1, newval);
    cm-> encode_heap_oop(rscratch1);
    cm-> lock();
    // cmpval (rax) is implicitly used by this instruction
    cm-> cmpxchgl(rscratch1, Address(addr, 0));

    cm-> jcc(Assembler::notZero, L_compare_first_fail);
    {
      addUpdateLog(masm, base, addr, cmpval);

      cm-> jmp(L_compare_done);
    }
    cm-> bind(L_compare_first_fail); 
    {
      cm-> shrq(cmpval, 32);
      cm-> testl(cmpval, 1);
    }
    cm-> bind(L_compare_dirty);
    { 
      cm-> jcc(Assembler::notEqual, L_compare_done);

      cm-> lock();
      cm-> cmpxchgl(rscratch1, Address(addr, 0));
    }
    cm-> bind(L_compare_done);
  } else
#endif
  {
    cm-> lock();
    cm-> cmpxchgptr(newval, Address(addr, 0));
  }
}



static LIR_Opr get_resolved_addr_reg(LIRAccess& access) {
  DecoratorSet decorators = access.decorators();
  LIRGenerator* gen = access.gen();
  bool is_array = (decorators & IS_ARRAY) != 0;
  bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;

  bool precise = is_array || on_anonymous;
  LIR_Opr addr = access.resolved_addr();

  if (addr->is_address()) {
    LIR_Address* address = addr->as_address_ptr();
    LIR_Opr resolved_addr = gen->new_pointer_register();
    if (!address->index()->is_valid() && address->disp() == 0) {
      __ move(address->base(), resolved_addr);
    } else {
      // assert(false && address->disp() != max_jint, "lea doesn't support patched addresses!");
      if (needs_patching) {
        __ leal(addr, resolved_addr, lir_patch_normal, access.patch_emit_info());
        access.clear_decorators(C1_NEEDS_PATCHING);
      } else {
        __ leal(addr, resolved_addr);
      }
      // access.set_resolved_addr(LIR_OprFact::address(new LIR_Address(resolved_addr, access.type())));
    }
    addr = resolved_addr;
  }
  assert(addr->is_register(), "must be a register at this point");
  return addr;
}

void rtgc_update_inverse_graph_c1(oopDesc* base, oopDesc* old_v, oopDesc* new_v);
namespace RTGC_unused { 
LIR_Opr update_inverse_graph(LIRGenerator* gen, LIR_Opr base, LIR_Opr old_v, LIR_Opr new_v) {

  BasicTypeList signature;
  signature.append(T_OBJECT); // base
  signature.append(T_OBJECT); // old_v
  signature.append(T_OBJECT); // new_v
  // signature.append(T_INT); // new_v
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base); 
  args->append(old_v); // cmp_value
  args->append(new_v);
  // args->append(LIR_OprFact::intConst(1));

  address fn = (address)__break;//rtgc_update_inverse_graph_c1;
  return gen->call_runtime(&signature, args, fn, voidType, NULL);
}
}

static LIR_Opr call_barrier(address fn, LIRAccess& access, LIR_Opr new_value, ValueType* result_type,
               LIR_Opr cmp_value = LIR_OprFact::illegalOpr) {
  DecoratorSet decorators = access.decorators();
  LIRGenerator* gen = access.gen();

  LIRItem& base = access.base().item();
  bool in_heap = decorators & IN_HEAP;
  bool compare = cmp_value->is_valid();
  LIR_Opr addr = get_resolved_addr_reg(access);

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  if (compare) signature.append(T_OBJECT); // cmp_value
  if (new_value != LIR_OprFact::illegalOpr) {
    signature.append(T_OBJECT); // new_value
  }
  if (in_heap) {
    signature.append(T_OBJECT); // object
    base.load_item();
  }
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr); 
  if (compare) args->append(cmp_value); // cmp_value
  if (new_value != LIR_OprFact::illegalOpr) {
    args->append(new_value);
  }
  if (in_heap) {
    args->append(base.result());
  }

  return gen->call_runtime(&signature, args, fn, result_type, NULL);
}

RtgcBarrierSetC1::RtgcBarrierSetC1() {
  RtgcBarrier::init_barrier_runtime();
}

bool RtgcBarrierSetC1::needBarrier_onResolvedAddress(LIRAccess& access, bool op_store) {
  return access.is_oop() && !RtgcBarrier::is_raw_access(access.decorators(), op_store)
      && !access.base().opr()->is_stack()
      && !access.resolved_addr()->is_stack();
}

LIR_Opr RtgcBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  // resolve_in_register |= access.is_oop()
  //     && !RtgcBarrier::is_raw_access(access.decorators(), false)
  //     && !access.base().opr()->is_stack();
  return BarrierSetC1::resolve_address(access, resolve_in_register);
}

class BarrierStub : public CodeStub {
  Label* _L_done;
public:  
  BarrierStub(Label* L_done) {
    this->_L_done = L_done;
  }

  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case();
    visitor->do_call();
    //if (_result_type != voidType) visitor->do_output(_phys_reg);
  }

  virtual void emit_code(LIR_Assembler* ce) {
    ce->masm()->bind(*entry());
    //ce->masm()->jmp(*_L_done);
    ce->masm()->jmp(*continuation());
  }

#ifndef PRODUCT
  virtual void print_name(outputStream* out) const {
    out->print("BarrierStub");
  }
#endif // PRODUCT

};

Address LIR_Assembler__as_Address(LIR_Address* addr) {
  precond (!addr->base()->is_illegal());
  if (addr->base()->is_illegal()) {
    // assert(addr->index()->is_illegal(), "must be illegal too");
    // AddressLiteral laddr((address)addr->disp(), relocInfo::none);
    // if (! __ reachable(laddr)) {
    //   __ movptr(tmp, laddr.addr());
    //   Address res(tmp, 0);
    //   return res;
    // } else {
    //   return __ as_Address(laddr);
    // }
  }

  Register base = addr->base()->as_pointer_register();

  if (addr->index()->is_illegal()) {
    return Address( base, addr->disp());
  } else if (addr->index()->is_cpu_register()) {
    Register index = addr->index()->as_pointer_register();
    return Address(base, index, (Address::ScaleFactor) addr->scale(), addr->disp());
  } else if (addr->index()->is_constant()) {
    intptr_t addr_offset = (addr->index()->as_constant_ptr()->as_jint() << addr->scale()) + addr->disp();
    assert(Assembler::is_simm32(addr_offset), "must be");
    return Address(base, addr_offset);
  } else {
    fatal("Unimplemented");
    return Address();
  }
}

class OopStoreStub : public CodeStub {
  address _fn;
  LIRAddressOpr _base;
  bool _in_heap;
  bool _cmpxchg;
public:
  LIR_OprList* _args;

  LIR_Opr _addr;
  LIR_Opr _phys_reg;
  LIR_Opr _base_item;
  LIR_Opr _value_item;
  LIR_Opr _tmp1, _tmp2;
  LIR_Op* _op;
  CallingConvention* cc;
  DecoratorSet _decorators;

  OopStoreStub(address fn, LIRAccess& access, LIR_Opr new_value, ValueType* result_type,
               LIRItem* cmp_item = NULL)
                : _fn(fn), _base(access.base()) {
    _op = NULL;
    _decorators = access.decorators();
    LIRGenerator* gen = access.gen();

    _in_heap = _decorators & IN_HEAP;
    _phys_reg = result_type->tag() == voidTag
                  ? LIR_OprFact::illegalOpr
                  : gen->result_register_for(result_type);
    _cmpxchg = cmp_item != NULL;

    bool patch = (_decorators & C1_NEEDS_PATCHING) != 0;
    precond(!patch);

    // BasicTypeList signature;
    // signature.append(T_ADDRESS); // addr
    // if (_cmpxchg) {
    //   signature.append(T_OBJECT); // cmp_value
    //   cmp_item->load_item();
    // }
    // signature.append(T_OBJECT); // new_value
    // if (_in_heap) {
    //   signature.append(T_OBJECT); // object
    //   _base.item().load_item();
    //   _base_item = _base.item().result();
    // }
    
    // cc = gen->frame_map()->c_calling_convention(&signature);
    // this->_args = cc->args();
    // LIR_List* lir = gen->lir();
    _addr = access.resolved_addr();
    precond(_addr->is_address());
    if (!_addr->is_address()) {
      assert(_addr->is_register(), "must be");
      _addr = LIR_OprFact::address(new LIR_Address(_addr, T_OBJECT));
    }

    LIR_Address* address = _addr->as_address_ptr();
    _value_item = new_value;

    //_tmp1 = _tmp2 = noreg;
    // if (address->index()->is_cpu_register()) {
    //   _tmp1 = gen->new_register(T_ADDRESS);
    //   _tmp2 = address->index();
    //   // printf("address->index()->is_cpu_register()\n");
    // } else {
      _tmp1 = gen->new_register(T_ADDRESS);
      _tmp2 = gen->new_register(T_ADDRESS);
      // precond(_tmp1->as_register() != _tmp2->as_register());
      // printf(" ! address->index()->is_cpu_register()\n");
    // }
    // precond(cc->at(0)->is_register());
    //   lir->move(_addr, cc->at(0));
    // int idx = 1;
    // if (_cmpxchg) lir->move(cmp_item->result(), cc->at(idx++));
    // lir->move(new_value, cc->at(idx++));
    // if (_in_heap) lir->move(_base.item().result(), cc->at(idx++));    
  }  

  LIR_Opr get_result(LIRGenerator* gen, ValueType* result_type) {
    LIR_Opr _result = gen->new_register(result_type);
    __ move(_phys_reg, _result);
    return _result;
  }

  virtual void visit(LIR_OpVisitState* visitor) {
    // visitor->do_input(_base_item);
    visitor->do_input(_value_item);
    visitor->do_input(_addr);
    visitor->do_temp(_tmp1);
    visitor->do_temp(_tmp2);
    // visitor->do_input(_addr);
    // visitor->do_slow_case();
    // visitor->do_call();
    if (_phys_reg->is_valid()) visitor->do_output(_phys_reg);
  }

  bool genConditionalAccessBranch(LIRGenerator* gen, BarrierSetC1* c1) {
    if (!_in_heap) {
      __ branch(lir_cond_always, this);
      return false;
    }

    LIR_Opr trackable_bit = LIR_OprFact::intConst(RTGC::TRACKABLE_BIT);
    LIR_Opr trMark = gen->new_register(T_INT);
    if (true) {
      LIR_Address* flag_addr =
        new LIR_Address(_base.item().result(),
                        offset_of(RTGC::GCNode, _flags),
                        T_INT);
      __ load(flag_addr, trMark);
    } else {
      LIR_Opr flags_offset = LIR_OprFact::intConst(offset_of(RTGC::GCNode, _flags));
      LIRAccess load_flag_access(gen, IN_HEAP, _base, flags_offset, T_INT, 
            NULL/*access.patch_emit_info()*/, NULL);//access.access_emit_info());  
      c1->BarrierSetC1::load_at(load_flag_access, trMark);
    }
    __ logical_and(trMark, trackable_bit, trMark);
    __ cmp(lir_cond_notEqual, trMark, LIR_OprFact::intConst(0));
    __ branch(lir_cond_notEqual, this->entry());
    return true;
  }

  static LIR_Opr get_resolved_addr_22(LIRAccess& access) {
    return access.resolved_addr();

    DecoratorSet decorators = access.decorators();
    LIRGenerator* gen = access.gen();
    bool is_array = (decorators & IS_ARRAY) != 0;
    bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
    bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;

    bool precise = is_array || on_anonymous;
    LIR_Opr addr = access.resolved_addr();

    if (addr->is_address()) {
      LIR_Address* address = addr->as_address_ptr();
      LIR_Opr resolved_addr = gen->new_pointer_register();
      if (!address->index()->is_valid() && address->disp() == 0) {
        __ move(address->base(), resolved_addr);
      } else {
        assert(false && address->disp() != max_jint, "lea doesn't support patched addresses!");
        if (needs_patching) {
          __ leal(addr, resolved_addr, lir_patch_normal, access.patch_emit_info());
          access.clear_decorators(C1_NEEDS_PATCHING);
        } else {
          __ leal(addr, resolved_addr);
        }
        access.set_resolved_addr(LIR_OprFact::address(new LIR_Address(resolved_addr, access.type())));
      }
      addr = resolved_addr;
    }
    assert(addr->is_register(), "must be a register at this point");
    return addr;
  }

  virtual void emit_code(LIR_Assembler* ce) {
    C1_MacroAssembler* cm = ce->masm();
    RtgcBarrierSetAssembler *bs = (RtgcBarrierSetAssembler*)BarrierSet::barrier_set()->barrier_set_assembler();
    ce->masm()->bind(*entry());

    //ce->masm()->jmp(*_L_done);
    Address addr = LIR_Assembler__as_Address(_addr->as_address_ptr());
    precond(_value_item->is_single_cpu() && !_value_item->is_virtual());
    precond(_tmp1->is_single_cpu() && !_tmp1->is_virtual());
    precond(_tmp2->is_single_cpu() && !_tmp2->is_virtual());
    bs->oop_store_at(cm, _decorators | C1_NEEDS_PATCHING, T_OBJECT,
                                         addr, 
                                         _value_item->as_register(), 
                                         _tmp1->as_register(), 
                                         _tmp2->as_register()
                                         );
    ce->masm()->jmp(*continuation());
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const {
    out->print("OopStoreStub");
  }
#endif // PRODUCT
};

LIR_Opr getTrackableMark(LIRAccess& access, BarrierSetC1* c1 = NULL) {
  LIRGenerator* gen = access.gen();
  LIRItem& base = access.base().item();
  //base.load_item();
  LIR_Opr trMark = gen->new_register(T_INT);
  LIR_Opr trackable_bit = LIR_OprFact::intConst(RTGC::TRACKABLE_BIT);

  if (true) {
    LIR_Address* flag_addr =
      new LIR_Address(base.result(),
                      offset_of(RTGC::GCNode, _flags),
                      T_INT);
    __ load(flag_addr, trMark);
  } else {
    LIR_Opr flags_offset = LIR_OprFact::intConst(offset_of(RTGC::GCNode, _flags));
    LIRAccess load_flag_access(gen, IN_HEAP, base, flags_offset, T_INT, 
          NULL/*access.patch_emit_info()*/, NULL);//access.access_emit_info());  
    c1->BarrierSetC1::load_at(load_flag_access, trMark);
  }
  return trMark;
}

bool genConditionalBarrierBranch(LIRAccess& access, Label* L_barrier, BarrierSetC1* c1 = NULL) {
  bool in_heap = (access.decorators() & IN_HEAP) != 0;
  if (!in_heap) {
    return false;
  }

  LIR_Opr trackable_bit = LIR_OprFact::intConst(RTGC::TRACKABLE_BIT);
  LIRGenerator* gen = access.gen();
  LIR_Opr trMark = getTrackableMark(access, c1);
  __ logical_and(trMark, trackable_bit, trMark);
  __ cmp(lir_cond_notEqual, trMark, LIR_OprFact::intConst(0));
  __ branch(lir_cond_notEqual, L_barrier);
  // __ branch(lir_cond_always, L_barrier);
  return true;
}

oopDesc* __rtgc_load(narrowOop* addr) {
  RTGC::lock_heap();
  narrowOop res = *addr;
  oopDesc* result = CompressedOops::decode(res);
  rtgc_log(LOG_OPT(1), "load (%p) => (%p:%s) th=%p\n",
      addr, result, 
      result ? result->klass()->name()->bytes() : NULL, JavaThread::current());
  RTGC::unlock_heap(true);
  return result;
}

void RtgcBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  DecoratorSet decorators = access.decorators();
  if (true || !needBarrier_onResolvedAddress(access, false)) {
    BarrierSetC1::load_at_resolved(access, result);
    return;
  }

  address fn = RtgcBarrier::getLoadFunction(decorators | AS_RAW);
  if (true) {
    LIR_Opr v = call_barrier(fn, access, ill, objectType);
    access.gen()->lir()->move(v, result);
  }
}


/* 참고) LIR_Op1::emit_code()
   -> LIR_Assembler::emit_op1() 
     -> LIR_Assembler::move_op()
        -> LIR_Assembler::mem2reg()
*/
void __rtgc_store(narrowOop* addr, oopDesc* new_value, oopDesc* base, oopDesc* old_value) {
  rtgc_log(LOG_OPT(2), "store %d] %p(%p) = %p th=%p\n", 
    rtgc_log_trigger, base, addr, new_value, (address)base + oopDesc::klass_offset_in_bytes());
  assert((address)addr <= (address)base + oopDesc::klass_offset_in_bytes(), "klass");
  RtgcBarrier::oop_store(addr, new_value, base);
}

void __rtgc_store_nih(narrowOop* addr, oopDesc* new_value, oopDesc* old_value) {
  printf("store __(%p) = %p\n", addr, new_value);
  RtgcBarrier::oop_store_not_in_heap(addr, new_value);
}

void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  if (!needBarrier_onResolvedAddress(access, true)) {
    LIR_Opr offset = access.offset().opr();
    bool setKlass = (access.decorators() & IN_HEAP) && 
                !(access.decorators() & IS_ARRAY) && 
                offset->is_constant() &&
                offset->as_jint() >= 0 &&
                offset->as_jint() <= oopDesc::klass_offset_in_bytes();
    assert(!setKlass, "just check");
    BarrierSetC1::store_at_resolved(access, value);
    return;
  }

  rtgc_log(LOG_OPT(11), "store_at_resolved\n");
  address fn = RtgcBarrier::getStoreFunction(access.decorators() | AS_RAW);
  bool in_heap = (access.decorators() & IN_HEAP) != 0;
  if (true || !in_heap) {
    call_barrier(fn, access, value, voidType);
    return;
  }
  else {
    LIRGenerator* gen = access.gen();
    fn = (address)__break;
    OopStoreStub* stub = new OopStoreStub(fn, access, value, voidType);
    bool needs_patching = (access.decorators() & C1_NEEDS_PATCHING) != 0;
    precond(!needs_patching);
    precond(access.access_emit_info() == NULL);
    if (false) {
      
      LIR_PatchCode patch_code = needs_patching ? lir_patch_normal : lir_patch_none;
      LIR_Address* addr = access.resolved_addr()->as_address_ptr();
      // __ store(value, addr, access.access_emit_info(), patch_code);
      stub->_op = new LIR_Op1(
              lir_move,
              stub->_args->at(1), // value,
              stub->_addr, // LIR_OprFact::address(addr),
              addr->type(),
              patch_code,
              access.access_emit_info());
      // LIR_Opr reg = stub->_args->at(1);
      // LIR_Opr result = gen->new_register(T_OBJECT);
      // __ move(reg, result);
    }
    // stub->_op = new LIR_Op2(lir_xchg, stub->cc->at(0), reg, reg, ill);
    // stub->_op = new LIR_Op2(lir_xchg, stub->_addr, result, result, ill);
    __ branch(lir_cond_always, stub);
    __ branch_destination(stub->continuation());
    return;
  }


}



oopDesc* __rtgc_xchg(volatile narrowOop* addr, oopDesc* new_value, oopDesc* base) {
  printf("xchg %p(%p) = %p\n", base, addr, new_value);
  return RtgcBarrier::oop_xchg(addr, new_value, base);
}

oopDesc* __rtgc_xchg_nih(volatile narrowOop* addr, oopDesc* new_value) {
  printf("xchg __(%p) = %p\n", addr, new_value);
  return RtgcBarrier::oop_xchg_not_in_heap(addr, new_value);
}

LIR_Opr RtgcBarrierSetC1::atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
  if (!needBarrier_onResolvedAddress(access, true)) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  if (true) {
    value.load_item();
    address fn = RtgcBarrier::getXchgFunction(access.decorators() | AS_RAW);
    return call_barrier(fn, access, value.result(), objectType);
  }

  LIRGenerator* gen = access.gen();
  address fn = RtgcBarrier::getXchgFunction(access.decorators());
  OopStoreStub* stub = new OopStoreStub(fn, access, value.result(), objectType);
  if (stub->genConditionalAccessBranch(gen, this)) {
    LIR_Opr addr = access.resolved_addr();
    LIR_Opr result = stub->_phys_reg;
    __ move(value.result(), result);
    // assert(type == T_INT || is_oop LP64_ONLY( || type == T_LONG ), "unexpected type");
    __ xchg(addr, result, result, LIR_OprFact::illegalOpr);
  }
  __ branch_destination(stub->continuation());
  return stub->get_result(gen, objectType);   
}

bool __rtgc_cmpxchg(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
  oopDesc* old_value = RtgcBarrier::oop_cmpxchg(addr, cmp_value, new_value, base);
  rtgc_log(cmp_value != NULL, "cmpxchg %p.%p = %p->%p %d\n", 
    base, addr, cmp_value, new_value, old_value == cmp_value);
  return old_value == cmp_value;
}

bool __rtgc_cmpxchg_nih(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value) {
  oopDesc* old_value = RtgcBarrier::oop_cmpxchg_not_in_heap(addr, cmp_value, new_value);
  rtgc_log(cmp_value != NULL, "cmpxchg @.%p = %p->%p %d\n", 
    addr, cmp_value, new_value, old_value == cmp_value);
  return old_value == cmp_value;
}

LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  if (!needBarrier_onResolvedAddress(access, true)) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  LIRGenerator* gen = access.gen();
  if (true) {
    new_value.load_item();
    cmp_value.load_item();
    address fn = RtgcBarrier::getCmpSetFunction(access.decorators() | AS_RAW);
    LIR_Opr result = call_barrier(fn, access, new_value.result(), objectType, cmp_value.result());
    __ cmp(lir_cond_equal, cmp_value.result(), result);
    __ cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
            result, T_INT);
    return result;
  }
  
  new_value.load_item();
  LIR_Opr result = gen->new_register(intType);
  
  if (true) {
    LIR_Opr addr = access.resolved_addr();
    cmp_value.load_item_force(FrameMap::rax_oop_opr);
    LIRItem& base = access.base().item();
    LIR_Opr base_opr = base.result();
    LIR_Opr cmp_opr = cmp_value.result();
    LIR_Opr new_opr = new_value.result();
    LIR_Opr addr_opr = addr->as_address_ptr()->base();
    // __ push(base_opr);

    __ append(new LIR_OpCompareAndSwapOop(base_opr, addr_opr, cmp_opr, new_opr, ill, ill, ill));

    LIR_Opr result = gen->new_register(intType);
    __ cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
          result, T_INT);
    // __ pop(base_opr);

    // LIR_Address update_log_top(gen->getThreadPointer(), (int)Thread::gc_data_offset(), T_ADDRESS);
    // LIR_Opr tmp = gen->new_register(addressType);
    // __ move(&update_log_top, tmp);
    // LIR_Opr ptr_size = LIR_OprFact::intConst(sizeof(void*));
    // LIR_Opr top_addr = gen->atomic_add(T_ADDRESS, tmp, ptr_size);
    // __ move(base_opr, new LIR_Address(top_addr, 0, T_ASDDRESS));
    // __ move(addr_opr, new LIR_Address(top_addr, 8));
    // __ move(cmp_opr, new LIR_Address(top_addr, 8));
  } else {
    address fn = RtgcBarrier::getCmpSetFunction(access.decorators());
    OopStoreStub* stub = new OopStoreStub(fn, access, new_value.result(), objectType, &cmp_value);
    __ move(cmp_value.result(), result);
    LabelObj* L_done = new LabelObj();
    if (stub->genConditionalAccessBranch(gen, this)) {
      LIR_Opr addr = access.resolved_addr();
      cmp_value.load_item_force(FrameMap::rax_oop_opr);
      __ cas_obj(addr->as_address_ptr()->base(), cmp_value.result(), new_value.result(), ill, ill);
      // __ branch(lir_cond_always, L_done->label());
      // LIR_Opr tmp = BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
      // __ move(tmp, gen->result_register_for(intType));
    } 
    __ branch_destination(stub->continuation());
    // __ cmp(lir_cond_equal, result, stub->get_result(gen, objectType));
    // __ branch_destination(L_done->label());
    __ cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
            result, T_INT);
  }
  return result;

  //return stub->get_result(gen, intType);   
}

const char* RtgcBarrierSetC1::rtcall_name_for_address(address entry) {
  #define FUNCTION_CASE(a, f) \
    if ((intptr_t)a == CAST_FROM_FN_PTR(intptr_t, f))  return #f

  // FUNCTION_CASE(entry, rtgc_oop_xchg_0);
  // FUNCTION_CASE(entry, rtgc_oop_xchg_3);
  // FUNCTION_CASE(entry, rtgc_oop_xchg_8);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_0);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_3);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_8);
  #undef FUNCTION_CASE

  return "RtgcRuntime::method";
}

