
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
#define ill   LIR_OprFact::illegalOpr

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_BARRIER_C1, function);
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
      assert(address->disp() != max_jint, "lea doesn't support patched addresses!");
      if (needs_patching) {
        gen->lir()->leal(addr, resolved_addr, lir_patch_normal, access.patch_emit_info());
        access.clear_decorators(C1_NEEDS_PATCHING);
      } else {
        gen->lir()->leal(addr, resolved_addr);
      }
      // access.set_resolved_addr(LIR_OprFact::address(new LIR_Address(resolved_addr, access.type())));
    }
    addr = resolved_addr;
  }
  assert(addr->is_register(), "must be a register at this point");
  return addr;
}

void rtgc_update_inverse_graph_c1(oopDesc* base, oopDesc* old_v, oopDesc* new_v);
oopDesc* _g3(oopDesc* base, oopDesc* old_v, oopDesc* new_v, bool do_lock) {
  fatal("_g3");
  return NULL;
}

static LIR_Opr update_inverse_graph(LIRGenerator* gen, LIR_Opr base, LIR_Opr old_v, LIR_Opr new_v) {

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

  address fn = (address)_g3;//rtgc_update_inverse_graph_c1;
  return gen->call_runtime(&signature, args, fn, voidType, NULL);
}

#define EX_OP 0
class OopStoreStub 
#if EX_OP
: public LIR_OpCompareAndSwap 
#else
: public CodeStub 
#endif
{
  LIR_Opr _new_v, _base, _old_v;
  bool _in_heap;
  address _fn;
  LIR_OprList* _args;
  LIR_Opr _result;
  LIR_Op* _op;
  CallingConvention* cc;
public:
  LIR_Opr _phys_reg;

  OopStoreStub(LIRGenerator* gen, LIR_Opr base, LIR_Opr addr, LIR_Opr old_v, LIR_Opr new_v, LIR_Op* op) 
#if EX_OP
    : LIR_OpCompareAndSwap(lir_cas_obj, addr, old_v, new_v, ill, ill, ill)
#endif
  {
    // DecoratorSet decorators = access.decorators();
    // LIRGenerator* gen = access.gen();

    // LIRItem base = access.base().item();
    // bool in_heap = decorators & IN_HEAP;

    _fn = (address)rtgc_update_inverse_graph_c1;
    _op = op;

    BasicTypeList signature;
    signature.append(T_OBJECT); // base
    signature.append(T_OBJECT); // old_v
    signature.append(T_OBJECT); // new_v
    
    _args = new LIR_OprList();
    _args->append(base); 
    _args->append(old_v); // cmp_value
    _args->append(new_v);

    cc = gen->frame_map()->c_calling_convention(&signature);
    for (int i = 0; i < _args->length(); i++) {
      LIR_Opr arg = _args->at(i);
      LIR_Opr loc = cc->at(i);
      if (arg == LIR_OprFact::illegalOpr) continue;

      if (loc->is_register()) {
        gen->lir()->move(arg, loc);
      } else {
        fatal("********************");
        LIR_Address* addr = loc->as_address_ptr();
        if (addr->type() == T_LONG || addr->type() == T_DOUBLE) {
          gen->lir()->unaligned_move(arg, addr);
        } else {
          gen->lir()->move(arg, addr);
        }
      }    
    }
    _phys_reg = LIR_OprFact::illegalOpr;
    //_phys_reg = FrameMap::as_opr(rscratch1); 
    //_phys_reg = gen->result_register_for(objectType);
  }  

  virtual void visit(LIR_OpVisitState* visitor) {
    // visitor->do_input(_new_value);
    // visitor->do_input(_addr);
    // visitor->do_input(_base);
    if (_op != NULL) visitor->visit(_op);

    // fatal("hghghghgghh");

    int n = _args->length();
    for (int i = 0; i < n; i++) {
      if (i != 1) {// !_args->at(i)->is_pointer()) {
        LIR_Opr opr = cc->at(i);
        // visitor->do_input(opr);
      }
    }
    visitor->do_slow_case();
    visitor->do_call();
    if (_phys_reg != LIR_OprFact::illegalOpr) {
      visitor->do_output(_phys_reg);
    }
  }

  virtual void emit_code(LIR_Assembler* ce) {
    Label L_done;
#if !EX_OP    
    ce->masm()->bind(*entry());
#endif
    if (EX_OP || _op != NULL) {
#if EX_OP     
      this->LIR_OpCompareAndSwap::emit_code(ce);
#else
      _op->emit_code(ce);
#endif
      ce->masm()->jcc(Assembler::notEqual, L_done);
    }
    ce->masm()->pushf();
    ce->masm()->push(rax);
    //ce->masm()->movptr(c_rarg0, );
    ce->masm()->movptr(c_rarg1, rax);
    //ce->masm()->movptr(c_rarg2, );

    ce->masm()->call(RuntimeAddress(_fn));
    ce->masm()->pop(rax);
    ce->masm()->popf();
    ce->masm()->bind(L_done);
#if !EX_OP   
    ce->masm()->jmp(*continuation());
#endif
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const {
    out->print("OopStoreStub");
  }
#endif // PRODUCT
};

static LIR_Opr call_barrier(address fn, LIRAccess& access, LIR_Opr new_value, ValueType* result_type,
               LIR_Opr cmp_value = LIR_OprFact::illegalOpr) {
  DecoratorSet decorators = access.decorators();
  LIRGenerator* gen = access.gen();

  LIRItem base = access.base().item();
  bool in_heap = decorators & IN_HEAP;
  bool compare = cmp_value->is_valid();
  LIR_Opr addr;;

  BasicTypeList signature;
  if (false && in_heap && compare) {
    signature.append(T_INT); // addr
    addr = access.offset().item().result();
  } else {
    addr = get_resolved_addr(access);
    signature.append(T_ADDRESS); // addr
  }
  if (compare) signature.append(T_OBJECT); // cmp_value
  signature.append(T_OBJECT); // new_value
  if (in_heap) {
    signature.append(T_OBJECT); // object
    base.load_item();
  }
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr); 
  if (compare) args->append(cmp_value); // cmp_value
  args->append(new_value);
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
      && !access.resolved_addr()->is_stack();
}

LIR_Opr RtgcBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  resolve_in_register |= access.is_oop()
      && !RtgcBarrier::is_raw_access(access.decorators(), false)
      && !access.base().opr()->is_stack();
  return BarrierSetC1::resolve_address(access, resolve_in_register);
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

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  
  LIR_OprList* args = new LIR_OprList();
  args->append(result);

  fatal("not_implemented");
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
void __rtgc_store(narrowOop* addr, oopDesc* new_value, oopDesc* base) {
  rtgc_log(true || LOG_OPT(2), "store-raw %p(%p) = %p\n", 
      base, addr, new_value);
  RawAccess<>::oop_store(addr, new_value);
}

void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  bool need_barrier = needBarrier_onResolvedAddress(access, true);
  if (!need_barrier) {
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
  DecoratorSet decorators = access.decorators();
  // bool is_volatile = (((decorators & MO_SEQ_CST) != 0) || AlwaysAtomicAccesses);
  // LIRGenerator* gen = access.gen();

  address fn = RtgcBarrier::getStoreFunction(decorators | AS_RAW);
  // if (is_volatile) {
  //   gen->lir()->membar_release();
  // }
  call_barrier(fn, access, value, voidType);
  // if (is_volatile && !support_IRIW_for_not_multiple_copy_atomic_cpu) {
  //   gen->lir()->membar();
  // }
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
  LIRGenerator* gen = access.gen();
  address fn = RtgcBarrier::getXchgFunction(access.decorators() | AS_RAW);
  LIR_Opr res = call_barrier(fn, access, value.result(), objectType);
  return res;   
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

  precond(is_reference_type(access.type()));
  if ((access.decorators() & IN_HEAP) == 0) {
    address fn = RtgcBarrier::getCmpSetFunction(access.decorators() | AS_RAW);
    LIR_Opr res = call_barrier(fn, access, new_value.result(), intType, cmp_value.result());
    return res;
  }

#if 1
  new_value.load_item();
  cmp_value.load_item();
  address fn = RtgcBarrier::getCmpSetFunction(access.decorators() | AS_RAW);
  LIR_Opr result = call_barrier(fn, access, new_value.result(), objectType, cmp_value.result());
  gen->lir()->cmp(lir_cond_equal, cmp_value.result(), result);
  gen->lir()->cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
           result, T_INT);
  return result;

#elif 1
  LIRGenerator *gen = access.gen();
  #define ____ gen->lir()->

  /*
    TEMP(old) ← *DEST
    IF accumulator(comp/ressult) = TEMP(old)
        THEN
            ZF ← 1;
            *DEST ← SRC(new_v);
        ELSE
            ZF ← 0;
            accumulator(comp/resut) ← TEMP( = *DEST);
    FI;
  */
  /*
  void G1BarrierSetC1::post_barrier(LIRAccess& access, LIR_OprDesc* addr, LIR_OprDesc* new_val) {
    if (addr->is_address()) {
      LIR_Address* address = addr->as_address_ptr();
      LIR_Opr ptr = gen->new_pointer_register();
      if (!address->index()->is_valid() && address->disp() == 0) {
        __ move(address->base(), ptr);
      } else {
        assert(address->disp() != max_jint, "lea doesn't support patched addresses!");
        __ leal(addr, ptr);
      }
      addr = ptr;
    }
    assert(addr->is_register(), "must be a register at this point");
  */
  // LIRItem base = access.base().item();
  // base.load_item();//_force(FrameMap::as_oop_opr(c_rarg0));
  LIR_Opr addr = access.resolved_addr();
  precond(addr->is_address());
  cmp_value.load_item_force(FrameMap::rax_oop_opr);
  new_value.load_item();//_force(FrameMap::as_oop_opr(c_rarg1));
  LIRItem& base = access.base().item();

#if EX_OP  
  LIR_Op* op = new OopStoreStub(gen, base.result(), addr->as_address_ptr()->base(), cmp_value.result(), new_value.result(), NULL);
  gen->lir()->append(op);
#else
  ____ cas_obj(addr->as_address_ptr()->base(), cmp_value.result(), new_value.result(), ill, ill);

  // base.load_item();//_force(FrameMap::as_oop_opr(c_rarg0));

      // LIR_Address* address = addr->as_address_ptr();
      // LIR_Opr ptr = gen->new_pointer_register();
      // if (!address->index()->is_valid() && address->disp() == 0) {
      //   fatal("!!!!!!!");
      //   gen->lir()->move(address->base(), ptr);
      // } else {
      //   assert(address->disp() != max_jint, "lea doesn't support patched addresses!");
      //   gen->lir()->move(address->base(), ptr);
      //   // gen->lir()->leal(addr, ptr);
      // }

  // OopStoreStub* stub = new OopStoreStub(gen, FrameMap::as_oop_opr(c_rarg0), ill, ill, new_value.result(), NULL);
  OopStoreStub* stub = new OopStoreStub(gen, ill, ill, ill, new_value.result(), NULL);
  gen->lir()->branch(lir_cond_equal, stub);
  gen->lir()->branch_destination(stub->continuation());
#endif
  LIR_Opr result = gen->new_register(T_INT);
  ____ cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
           result, T_INT);
  return result;   

#else

  BasicType type = access.type();
  LIRItem base = access.base().item();
  // base.load_item();
  LIR_Opr addr = access.resolved_addr();
    
  LIR_Op* op = new LIR_OpCompareAndSwap(lir_cas_obj, addr->as_address_ptr()->base(), cmp_value.result(), new_value.result(), ill, ill, ill);

  LabelObj* L_fail = new LabelObj();
  LabelObj* L_done = new LabelObj();

  // gen->lir()->branch(lir_cond_notEqual, L_fail->label());
  // update_inverse_graph(gen, base.result(), FrameMap::rax_oop_opr, new_value.result());
  
  // OopStoreStub* stub = new OopStoreStub(gen, base.result(), cmp_value.result(), new_value.result(), op);
  OopStoreStub* stub = new OopStoreStub(gen, ill, ill, ill, op);
#if 1
  gen->lir()->branch(lir_cond_always, stub);
  gen->lir()->branch_destination(stub->continuation());

#else
  gen->lir()->branch(lir_cond_equal, stub->entry());
  gen->lir()->move(LIR_OprFact::intConst(0), result);
  gen->lir()->branch(lir_cond_always, L_done->label());
  
  gen->lir()->branch(lir_cond_always, stub);
  //gen->lir()->app masm()->append_code_stub(stub);
  gen->lir()->branch_destination(stub->continuation());
  gen->lir()->move(LIR_OprFact::intConst(1), result);
  // gen->lir()->branch(lir_cond_always, L_done->label());

  // gen->lir()->branch_destination(L_fail->label());
  // gen->lir()->move(LIR_OprFact::intConst(0), result);
  gen->lir()->branch_destination(L_done->label());
#endif
  LIR_Opr result = gen->new_register(T_INT);
  ____ cmove(lir_cond_equal, LIR_OprFact::intConst(1), LIR_OprFact::intConst(0),
           result, T_INT);
  return result;   
#endif
}

const char* RtgcBarrierSetC1::rtcall_name_for_address(address entry) {
  return "RtgcRuntime::method";
}

LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  LIRGenerator *gen = access.gen();
  bool need_barrier = access.is_oop()
      && !RtgcBarrier::is_raw_access(access.decorators(), false)
      && !access.base().opr()->is_stack();

  // DecoratorSet decorators = access.decorators();
  // bool in_heap = (decorators & IN_HEAP) != 0;
  // assert(in_heap, "not supported yet");

  // access.load_base();
  // access.offset().item().load_item();

  // LIR_Opr resolved = resolve_address(access, !need_barrier);
  // access.set_resolved_addr(resolved);
  // return atomic_cmpxchg_at_resolved(access, cmp_value, new_value);


  // if (need_barrier) {
  //   LIRItem& base = access.base().item();
  //   base.load_item();
  //   gen->lir()->move(base.result(), FrameMap::as_oop_opr(c_rarg0));

  // }
  {
    return BarrierSetC1::atomic_cmpxchg_at(access, cmp_value, new_value);
  }
}