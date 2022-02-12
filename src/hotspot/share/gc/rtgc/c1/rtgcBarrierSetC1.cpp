
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
#include "asm/assembler.inline.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
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


class OopStoreStub : public CodeStub {
private:
  DecoratorSet  _decorators;
  LIR_Opr     _addr, _new_value, _cmp_value, _base;
  LIR_Opr     _loc0, _loc1, _loc2, _loc3;
  LIR_MoveKind _move_kind;
public:
  OopStoreStub(LIRAccess& access, LIR_Opr value) : _new_value(value) {
    _decorators = access.decorators();
    bool in_heap = _decorators & IN_HEAP;

    BasicTypeList signature;
    signature.append(T_ADDRESS); // addr
    signature.append(T_OBJECT); // new_value
    if (in_heap) {
      signature.append(T_OBJECT); // object
    }
    
    _base = access.base().item().result();
    _addr = get_resolved_addr(access);
    CallingConvention* cc;    
    cc = access.gen()->frame_map()->c_calling_convention(&signature);
    _loc0 = cc->at(0);
    _loc1 = cc->at(1);
    if (in_heap) {
      _loc2 = cc->at(2);
    }

    LIR_List* lir = access.gen()->lir();
    if (_loc0->is_register()) {
      lir->move(_addr, _loc0);
    }
    else {
      LIR_Address* addr = _loc0->as_address_ptr();
      if (addr->type() == T_LONG || addr->type() == T_DOUBLE) {
        lir->unaligned_move(_addr, addr);
      } else {
        lir->move(_addr, addr);
      }
    }
    lir->move(_new_value, _loc1);
    if (in_heap) {
      lir->move(_base, _loc2);
    }
  }  

  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case();
    visitor->do_call();
    visitor->do_input(_loc0);
    visitor->do_input(_loc1);
    bool in_heap = _decorators & IN_HEAP;
    if (in_heap)
    visitor->do_input(_loc2);
    //visitor->do_temp()
    // if (_loc0->is_register())
    //   visitor->do_temp(_loc0);
    // visitor->do_temp(_loc1);
    // visitor->do_temp(_loc2);
  }

static void __checkRawAccess(MacroAssembler* masm, Register obj, Label& rawAccess) {
  ByteSize offset_gc_flags = in_ByteSize(offset_of(RTGC::GCNode, _flags));
  RTGC::GCFlags _flags;
  *(int*)&_flags = 0;
  _flags.isPublished = true;

  Register tmp3 = LP64_ONLY(r8) NOT_LP64(rsi);

  masm->movl(tmp3, Address(obj, offset_gc_flags));
  masm->andl(tmp3, *(int*)&_flags);
  // notZero 바꿔서 test.
  masm->jcc(Assembler::zero, rawAccess);

}

  virtual void emit_code(LIR_Assembler* ce) {
    ce->masm()->bind(*entry());
    bool in_heap = _decorators & IN_HEAP;

    address fn = RtgcBarrier::getStoreFunction(_decorators);

    if (in_heap) {
      Label L_rawAccess, L_done;
      __checkRawAccess(ce->masm(), c_rarg2, L_rawAccess);
      ce->masm()->call(RuntimeAddress(fn));
      ce->masm()->jmp(L_done);
      ce->masm()->bind(L_rawAccess);
      BarrierSet::barrier_set()->barrier_set_assembler()->oop_store_at(      
          ce->masm(), _decorators | AS_RAW, T_OBJECT, Address(_loc0->as_register(), 0), 
          _loc1->as_register(), noreg, noreg);
      ce->masm()->bind(L_done);
    }
    else {
      ce->masm()->call(RuntimeAddress(fn));
    }
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

  // address fn = RtgcBarrier::getPostLoadFunction(in_heap);

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
  // if (new_value == old_value) return;
  RtgcBarrier::oop_store(addr, new_value, base);
}

void __rtgc_store_nih(narrowOop* addr, oopDesc* new_value, oopDesc* old_value) {
  //assert(addr == base, "klass");
  // if (new_value == old_value) return;
  printf("store __(%p) = %p\n", addr, new_value);
  RtgcBarrier::oop_store_not_in_heap(addr, new_value);
}

void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  DecoratorSet decorators = access.decorators();
  bool is_array = (decorators & IS_ARRAY) != 0;
  bool in_heap = decorators & IN_HEAP;
  bool is_volatile = (((decorators & MO_SEQ_CST) != 0) || AlwaysAtomicAccesses);
  
  LIR_Opr offset = access.offset().opr();
  bool setKlass = in_heap && !is_array && 
              offset->is_constant() &&
              offset->as_jint() >= 0 &&
              offset->as_jint() <= oopDesc::klass_offset_in_bytes();

  if (!needBarrier_onResolvedAddress(access) || setKlass) {
    assert(!setKlass, "just check");
    BarrierSetC1::store_at_resolved(access, value);
    return;
  }

  LIRGenerator* gen = access.gen();


  // LIR_Opr flags_offset = LIR_OprFact::intConst(offset_of(RTGC::GCNode, _flags));
  // RTGC::GCFlags _flags;
  // *(int*)&_flags = 0;
  // _flags.isPublished = true;
  // LIR_Opr flag_old = LIR_OprFact::intConst(*(int*)&_flags);

  // LabelObj* L_trace = new LabelObj();
  // LabelObj* L_done = new LabelObj();
  OopStoreStub* stub = new OopStoreStub(access, value);

  // LIR_Opr tmpValue = gen->new_register(T_INT);
  // LIRAccess load_flag_access(gen, decorators, access.base(), flags_offset, T_INT, 
  //       NULL/*access.patch_emit_info()*/, NULL);//access.access_emit_info());  
  // BarrierSetC1::load_at(load_flag_access, tmpValue);
  // gen->lir()->logical_and(tmpValue, flag_old, tmpValue);

  // gen->lir()->cmp(lir_cond_equal, tmpValue, LIR_OprFact::intConst(0));
  gen->lir()->branch(lir_cond_always, stub);
    // BarrierSetC1::store_at_resolved(access, value);
    // gen->lir()->branch(lir_cond_always, L_done->label());
  gen->lir()->branch_destination(stub->continuation());
  //   BarrierSetC1::store_at_resolved(access, value);
  // gen->lir()->branch_destination(L_done->label());
  return;    
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
  DecoratorSet decorators = access.decorators();
  if (!needBarrier_onResolvedAddress(access)) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  bool in_heap = decorators & IN_HEAP;
  if (in_heap) base.load_item();
  LIR_Opr addr = get_resolved_addr(access);
  value.load_item();

  BasicTypeList signature;
  signature.append(T_INT); // addr
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT);    // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr);//.result());
  args->append(value.result());
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getXchgFunction(in_heap);
  // if (in_heap)
  //   fn = reinterpret_cast<address>(__rtgc_xchg);
  // else 
  //   fn = reinterpret_cast<address>(__rtgc_xchg_nih);

  LIR_Opr res = gen->call_runtime(&signature, args,
                fn,
                objectType, NULL);
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
  DecoratorSet decorators = access.decorators();
  if (!needBarrier_onResolvedAddress(access)) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  LIRGenerator* gen = access.gen();
  bool in_heap = decorators & IN_HEAP;
  LIRItem base = access.base().item();
  LIR_Opr addr = get_resolved_addr(access);
  cmp_value.load_item();
  new_value.load_item();
  if (in_heap) base.load_item();

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  signature.append(T_OBJECT); // cmp_value
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT); // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr);
  args->append(cmp_value.result());
  args->append(new_value.result());
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getCmpSetFunction(in_heap);
  // if (in_heap)
  //   fn = reinterpret_cast<address>(__rtgc_cmpxchg);
  // else 
  //   fn = reinterpret_cast<address>(__rtgc_cmpxchg_nih);

  LIR_Opr res = gen->call_runtime(&signature, args,
              fn,
              intType, NULL);
  return res;    
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