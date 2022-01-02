
#if 0
#include "precompiled.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/rtgc/rtgcBarrierSet.inline.hpp"
#include "gc/rtgc/rtgcBarrierSetAssembler.hpp"
#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"

#define __ masm->masm()->

void LIR_OpRtgcCompareAndSwap::emit_code(LIR_Assembler* masm) {
  NOT_LP64(assert(_addr->is_single_cpu(), "must be single");)
  Register addr = _addr->is_single_cpu() ? _addr->as_register() : _addr->as_register_lo();
  Register newval = _new_value->as_register();
  Register cmpval = _cmp_value->as_register();
  Register tmp1 = _tmp1->as_register();
  Register tmp2 = _tmp2->as_register();
  Register result = result_opr()->as_register();
  assert(cmpval == rax, "wrong register");
  assert(newval != NULL, "new val must be register");
  assert(cmpval != newval, "cmp and new values must be in different registers");
  assert(cmpval != addr, "cmp and addr must be in different registers");
  assert(newval != addr, "new value and addr must be in different registers");

  // Apply storeval barrier to newval.
  RtgcBarrierSet::assembler()->storeval_barrier(masm->masm(), newval, tmp1);

#ifdef _LP64
  if (UseCompressedOops) {
    __ encode_heap_oop(cmpval);
    __ mov(rscratch1, newval);
    __ encode_heap_oop(rscratch1);
    newval = rscratch1;
  }
#endif

  RtgcBarrierSet::assembler()->cmpxchg_oop(masm->masm(), result, Address(addr, 0), cmpval, newval, false, tmp1, tmp2);
}

#undef __

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  if (!access.is_oop()) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  LIRGenerator* gen = access.gen();
  LIR_Opr base = access.base().opr()
  LIR_Opr addr = access.resolved_addr()->as_address_ptr()->base();
  op->addr()->as_register();
  comapre_value.load_item();
  new_value.load_item();

  BasicTypeList signature;
  signature.append(T_OBJECT);    // object
  signature.append(T_INT); // offset
  signature.append(T_OBJECT); // comapre_value
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base);
  args->append(filed_ptr);
  args->append(compare_value.result());
  args->append(new_value.result());

  LIR_Opr res = gen->call_runtime(&signature, args,
              CAST_FROM_FN_PTR(address, RTGC::RTGC_CmpXchgObjField),
              objectType, NULL);
  }
  return res;    
}

LIR_Opr RtgcBarrierSetC1::atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
  if (!access.is_oop()) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  LIRGenerator* gen = access.gen();
  LIR_Opr base = access.base().opr()
  LRI_Opr addr = access.resolved_addr();
  op->addr()->as_register();
  value.load_item();

  BasicTypeList signature;
  signature.append(T_OBJECT);    // object
  signature.append(T_INT); // offset
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base);
  args->append(filed_ptr);
  args->append(value.result());

  LIR_Opr res = gen->call_runtime(&signature, args,
              CAST_FROM_FN_PTR(address, RTGC::RTGC_XchgObjField),
              objectType, NULL);
  }
  return res;    
}
#endif 