
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
static const bool USE_REG_ADDR = true;
static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_BARRIER_C1, function);
}

void __trace(narrowOop* addr) {
  //rtgc_log(1, "__TRCAE %p\n", addr);
}
void __break(narrowOop* addr) {
  assert(false, "__BREAK %p\n", addr);
}

static LIR_Opr get_resolved_addr_reg(LIRAccess& access) {
  return ((RtgcBarrierSetC1*)0)->get_resolved_addr_reg(access);
}

class OopStoreStub : public CodeStub {
  LIR_Opr _addr;
  LIR_Opr _phys_reg;
  LIR_Opr _base;
  LIR_Opr _value;
  LIR_Opr _cmp_v;
  RtgcBarrierSetAssembler::ReplaceType _type;
  DecoratorSet _decorators;
public:
  OopStoreStub(LIRAccess& access, LIR_Opr new_value) {
    _decorators = access.decorators();
    LIRGenerator* gen = access.gen();

    _phys_reg = _cmp_v = NULL;
    _type = RtgcBarrierSetAssembler::ReplaceType::Store;
    _value = gen->new_register(T_ADDRESS);
    gen->lir()->move(new_value, _value);

    if (USE_REG_ADDR) {
      _addr = get_resolved_addr_reg(access);
    } else {
      _addr = access.resolved_addr();
    }

    bool in_heap = (_decorators & IN_HEAP) != 0;
    if (in_heap) {
      LIRItem& base = access.base().item();
      base.load_item();
      _base = gen->new_register(T_ADDRESS);
      gen->lir()->move(base.result(), _base);
    } else {
      _base = NULL;
    }
    // _tmp1 = gen->new_register(T_ADDRESS);
  }  

  LIR_Opr prepare_atomic_result(LIRGenerator* gen, LIRItem* cmp_item) {
    _phys_reg = _value;
    debug_only(_decorators |= C1_NEEDS_PATCHING);
    if (cmp_item == NULL) {
      _type = RtgcBarrierSetAssembler::ReplaceType::Xchg;
    } else {
      _type = RtgcBarrierSetAssembler::ReplaceType::CmpXchg;
      cmp_item->load_item();
      _cmp_v = gen->new_register(T_ADDRESS);
      gen->lir()->move(cmp_item->result(), _cmp_v);
    }
    return _phys_reg;
  }

  LIR_Opr get_result(LIRGenerator* gen, ValueType* result_type) {
    LIR_Opr _result = gen->new_register(result_type);
    __ move(_phys_reg, _result);
    return _result;
  }

  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case();
    if (!_value->is_constant()) {
      visitor->do_input(_value);
      visitor->do_temp(_value);
    }
    if (USE_REG_ADDR && _base != NULL) {
      visitor->do_input(_base);
    }
    visitor->do_input(_addr);
    visitor->do_temp(_addr);
    // visitor->do_input(_tmp1);
    // visitor->do_temp(_tmp1);
    if (_phys_reg != NULL) {
      visitor->do_output(_phys_reg);
    }
  }

  virtual void emit_code(LIR_Assembler* ce) {
    C1_MacroAssembler* cm = ce->masm();
    RtgcBarrierSetAssembler *bs = (RtgcBarrierSetAssembler*)BarrierSet::barrier_set()->barrier_set_assembler();
    ce->masm()->bind(*entry());

    if (USE_REG_ADDR) {
      bs->oop_replace_at(cm, _decorators,
                        _base->as_register(), 
                        _addr->as_register(), 
                        _value->as_register(), 
                        _type,
                        _cmp_v == NULL ? noreg : _cmp_v->as_register());
    } else {
      fatal("deprecated.");
      // Address addr = LIR_Assembler__as_Address(_addr->as_address_ptr());
      // bs->oop_store_at(cm, _decorators | C1_NEEDS_PATCHING, T_OBJECT,
      //                                    addr, 
      //                                    !_value ? noreg : _value->as_register(), 
      //                                    noreg, noreg //_tmp1->as_register()
      //                                    );
    }
    ce->masm()->jmp(*continuation());
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const {
    out->print("OopStoreStub");
  }
#endif // PRODUCT
};



LIR_Opr RtgcBarrierSetC1::get_resolved_addr_reg(LIRAccess& access) {
  LIR_Address* address = NULL;
  LIR_Opr addr_op = access.resolved_addr();
  if (addr_op != NULL) {
    precond(addr_op->is_address());
    address = addr_op->as_address_ptr();
    if (!address->base()->is_register() || address->index()->is_valid() || address->disp() != 0) {
      address = NULL;
    }
  }
  if (address == NULL) {
    addr_op = BarrierSetC1::resolve_address(access, true);
    access.set_resolved_addr(addr_op);
    address = addr_op->as_address_ptr();
  }
  precond(address->base()->is_register()); 
  precond(!address->index()->is_valid() && address->disp() == 0);
  
  LIR_Opr addr_reg = access.gen()->new_register(T_ADDRESS);
  access.gen()->lir()->move(address->base(), addr_reg);
  return addr_reg;
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
  if (RtgcBarrier::is_raw_access(access.decorators(), op_store) ||
      !access.is_oop() || 
      access.base().opr()->is_stack() ||
      access.resolved_addr()->is_stack()) {
    return false;
  }
  if ((access.decorators() & IS_ARRAY) == 0) {
    LIR_Opr offset = access.offset().opr();
    if (offset->is_constant()) {
      LIR_Const* const_opr = offset->as_constant_ptr();
      jlong disp;
      if (const_opr->type() == T_INT) {
        disp = const_opr->as_jint();
      } else if (const_opr->type() == T_LONG) {
        disp = const_opr->as_jlong();
      } else {
        assert(const_opr->type() == T_INT, "const type = %d\n", const_opr->type());
      }
      assert(disp < 0 || disp > oopDesc::klass_offset_in_bytes(), "just checking");
      return disp < 0 || disp > oopDesc::klass_offset_in_bytes();
    }
  }
  return true;
}

LIR_Opr RtgcBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  return BarrierSetC1::resolve_address(access, resolve_in_register);
}


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

/* 참고) LIR_Op1::emit_code()
   -> LIR_Assembler::emit_op1() 
     -> LIR_Assembler::move_op()
        -> LIR_Assembler::mem2reg()
*/
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
  } else {

  }
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

  bool in_heap = (access.decorators() & IN_HEAP) != 0;
  if (0 || !in_heap) {
    address fn = RtgcBarrier::getStoreFunction(access.decorators() | AS_RAW);
    call_barrier(fn, access, value, voidType);
    return;
  } else {
    LIRGenerator* gen = access.gen();
    OopStoreStub* stub = new OopStoreStub(access, value);
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

  value.load_item();
  bool in_heap = (access.decorators() & IN_HEAP) != 0;
  if (0 || !in_heap) {
    address fn = RtgcBarrier::getXchgFunction(access.decorators() | AS_RAW);
    return call_barrier(fn, access, value.result(), objectType);
  } else {
    fatal("atomic_xchg_at_resolved");
    LIRGenerator* gen = access.gen();
    OopStoreStub* stub = new OopStoreStub(access, value.result());
    LIR_Opr phys_reg = stub->prepare_atomic_result(gen, NULL);
    __ branch(lir_cond_always, stub);
    __ branch_destination(stub->continuation());
    LIR_Opr result = gen->new_register(T_OBJECT);
    __ move(phys_reg, result);
    return result;
  }

  // LIRGenerator* gen = access.gen();
  // address fn = RtgcBarrier::getXchgFunction(access.decorators());
  // OopStoreStub* stub = new OopStoreStub(fn, access, value.result(), objectType);
  // if (stub->genConditionalAccessBranch(gen, this)) {
  //   LIR_Opr addr = access.resolved_addr();
  //   LIR_Opr result = stub->_phys_reg;
  //   __ move(value.result(), result);
  //   // assert(type == T_INT || is_oop LP64_ONLY( || type == T_LONG ), "unexpected type");
  //   __ xchg(addr, result, result, LIR_OprFact::illegalOpr);
  // }
  // __ branch_destination(stub->continuation());
  // return stub->get_result(gen, objectType);   
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
  new_value.load_item();
  cmp_value.load_item();

  bool in_heap = (access.decorators() & IN_HEAP) != 0;
  if (1 || !in_heap) {
    address fn = RtgcBarrier::getCmpSetFunction(access.decorators() | AS_RAW);
    LIR_Opr result = call_barrier(fn, access, new_value.result(), objectType, cmp_value.result());
    __ cmp(lir_cond_equal, cmp_value.result(), result);
    __ cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
            result, T_INT);
    return result;
  } else {
    fatal("atomic_cmpxchg_at_resolved");
    return NULL;
  }

  // new_value.load_item();
  // LIR_Opr result = gen->new_register(intType);
  
  // if (true) {
  // } else {
  //   address fn = RtgcBarrier::getCmpSetFunction(access.decorators());
  //   OopStoreStub* stub = new OopStoreStub(fn, access, new_value.result(), objectType, &cmp_value);
  //   __ move(cmp_value.result(), result);
  //   LabelObj* L_done = new LabelObj();
  //   if (stub->genConditionalAccessBranch(gen, this)) {
  //     LIR_Opr addr = access.resolved_addr();
  //     cmp_value.load_item_force(FrameMap::rax_oop_opr);
  //     __ cas_obj(addr->as_address_ptr()->base(), cmp_value.result(), new_value.result(), ill, ill);
  //   } 
  //   __ branch_destination(stub->continuation());
  //   __ cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
  //           result, T_INT);
  // }
  // return result;

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

