
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
#include "runtime/interfaceSupport.inline.hpp"
#include "asm/assembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#ifdef COMPILER1
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/z/c1/zBarrierSetC1.hpp"
#endif // COMPILER1

#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"

static int rtgc_log_trigger = 0;
static const bool ENABLE_CPU_MEMBAR = false;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_BARRIER_C1, function);
}

// class OpOppStore : public LIR_Op1 {
//   OpRawStore(LIR_Code code, LIR_Opr src, LIR_Address* addr, 
//             LIR_Opr result, LIR_PatchCode patch = lir_patch_none)
//   : LIR_Op1(code, src, LIR_OprFact::address(addr),
//             T_OBJECT, patch_code, NULL) {}
//   virtual void emit_code(LIR_Assembler* masm) {
//     masm->bind(_entry);
//     LIR_Op1::emit_code();
//   }
// };


RtgcBarrierSetC1::RtgcBarrierSetC1() {
  RtgcBarrier::init_barrier_runtime();
}

bool RtgcBarrierSetC1::needBarrier_onResolvedAddress(LIRAccess& access) {
  return access.is_oop() && !RtgcBarrier::is_raw_access(access.decorators())
      && !access.resolved_addr()->is_stack();
}


LIR_Opr RtgcBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  resolve_in_register |= access.is_oop()
      && !RtgcBarrier::is_raw_access(access.decorators())
      && !access.base().opr()->is_stack();
  return BarrierSetC1::resolve_address(access, resolve_in_register);
}

class OopStoreStub : public CodeStub {
  address _fn;
  LIR_OprList* _args;
  LIRAddressOpr _base_addr;
  bool _in_heap;
public:

  OopStoreStub(address fn, LIRAccess& access, LIR_Opr value, 
               LIR_Opr cmp_value = LIR_OprFact::illegalOpr)
                : _fn(fn), _base_addr(access.base()) {
    DecoratorSet decorators = access.decorators();
    bool _in_heap = decorators & IN_HEAP;
    bool compare = cmp_value->is_valid();

    BasicTypeList signature;
    signature.append(T_ADDRESS); // addr
    signature.append(T_OBJECT); // new_value
    if (compare) signature.append(T_OBJECT); // cmp_value
    if (_in_heap) {
      signature.append(T_OBJECT); // object
      _base_addr.item().load_item();
    }
    
    LIRGenerator* gen = access.gen();
    CallingConvention* cc = gen->frame_map()->c_calling_convention(&signature);
    this->_args = cc->args();
    LIR_List* lir = gen->lir();
    LIR_Opr p = get_resolved_addr(access);
    if (cc->at(0)->is_register()) {
      lir->move(p, cc->at(0));
    } else {
      LIR_Address* addr = p->as_address_ptr();
      if (addr->type() == T_LONG || addr->type() == T_DOUBLE) {
        lir->unaligned_move(addr, cc->at(0));
      } else {
        lir->move(addr, cc->at(0));
      }
    }

    lir->move(value, cc->at(1));
    int idx = 2;
    if (compare) lir->move(cmp_value, cc->at(idx++));
    if (_in_heap) lir->move(_base_addr.item().result(), cc->at(idx++));    
  }  

  LIR_Opr get_result(LIRGenerator* gen, ValueType* result_type) {
    LIR_Opr _phys_reg = gen->result_register_for(result_type);
    LIR_Opr _result = gen->new_register(result_type);
    gen->lir()->move(_phys_reg, _result);
    return _result;
  }

  virtual void visit(LIR_OpVisitState* visitor) {
    int n = _args->length();
    for (int i = 0; i < n; i++) {
      if (!_args->at(i)->is_pointer()) {
        visitor->do_input(*_args->adr_at(i));
      }
    }
    visitor->do_slow_case();
    visitor->do_call();
    // if (_phys_reg->is_valid()) visitor->do_output(_phys_reg);
  }

  bool genConditionalAccessBranch(LIRGenerator* gen, BarrierSetC1* c1) {
    if (!_in_heap) {
      gen->lir()->branch(lir_cond_always, this->entry());
      return false;
    }

    LIR_Opr flags_offset = LIR_OprFact::intConst(offset_of(RTGC::GCNode, _flags));
    LIR_Opr trackable_bit = LIR_OprFact::intConst(RTGC::TRACKABLE_BIT);
    LIRAccess load_flag_access(gen, IN_HEAP, _base_addr, flags_offset, T_INT, 
          NULL/*access.patch_emit_info()*/, NULL);//access.access_emit_info());  
    LIR_Opr tmpValue = gen->new_register(T_INT);

    c1->BarrierSetC1::load_at(load_flag_access, tmpValue);
    gen->lir()->logical_and(tmpValue, trackable_bit, tmpValue);
    gen->lir()->cmp(lir_cond_notEqual, tmpValue, LIR_OprFact::intConst(0));
    gen->lir()->branch(lir_cond_notEqual, this->entry());
    return true;        
  }

  static LIR_Opr get_resolved_addr(LIRAccess& access) {
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
        gen->lir()->move(address->base(), resolved_addr);
      } else {
        assert(false && address->disp() != max_jint, "lea doesn't support patched addresses!");
        if (needs_patching) {
          gen->lir()->leal(addr, resolved_addr, lir_patch_normal, access.patch_emit_info());
          access.clear_decorators(C1_NEEDS_PATCHING);
        } else {
          gen->lir()->leal(addr, resolved_addr);
        }
        access.set_resolved_addr(LIR_OprFact::address(new LIR_Address(resolved_addr, access.type())));
      }
      addr = resolved_addr;
    }
    assert(addr->is_register(), "must be a register at this point");
    return addr;
  }

  virtual void emit_code(LIR_Assembler* ce) {
    ce->masm()->bind(*entry());
    ce->masm()->call(RuntimeAddress(_fn));
    ce->masm()->jmp(*continuation());
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const {
    out->print("OopStoreStub");
  }
#endif // PRODUCT
};


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
  if (true || !needBarrier_onResolvedAddress(access)) {
    BarrierSetC1::load_at_resolved(access, result);
    return;
  }

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  
  LIR_OprList* args = new LIR_OprList();
  args->append(result);

  // address fn = RtgcBarrier::getPostLoadFunction(docorators);

  // LIR_Opr res = gen->call_runtime(&signature, args,
  //             fn,
  //             objectType, NULL);
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
  if (!needBarrier_onResolvedAddress(access)) {
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

  LIRGenerator* gen = access.gen();
  address fn = RtgcBarrier::getStoreFunction(access.decorators());
  OopStoreStub* stub = new OopStoreStub(fn, access, value);
  if (stub->genConditionalAccessBranch(gen, this)) {
    BarrierSetC1::store_at_resolved(access, value);
  }
  gen->lir()->branch_destination(stub->continuation());
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
  if (!needBarrier_onResolvedAddress(access)) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  value.load_item();
  LIRGenerator* gen = access.gen();
  address fn = RtgcBarrier::getXchgFunction(access.decorators());
  OopStoreStub* stub = new OopStoreStub(fn, access, value.result());
  if (stub->genConditionalAccessBranch(gen, this)) {
    LIR_Opr tmp = BarrierSetC1::atomic_xchg_at_resolved(access, value);
    gen->lir()->move(tmp, gen->result_register_for(objectType));
  }
  gen->lir()->branch_destination(stub->continuation());
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
  if (!needBarrier_onResolvedAddress(access)) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  // access.base().item().load_item();
  new_value.load_item();
  cmp_value.load_item();
  LIRGenerator* gen = access.gen();
  address fn = RtgcBarrier::getCmpSetFunction(access.decorators());
  OopStoreStub* stub = new OopStoreStub(fn, access, new_value.result(), cmp_value.result());
  if (stub->genConditionalAccessBranch(gen, this)) {
    LIR_Opr tmp = BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
    gen->lir()->move(tmp, gen->result_register_for(intType));
  } 
  gen->lir()->branch_destination(stub->continuation());
  return stub->get_result(gen, intType);   
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