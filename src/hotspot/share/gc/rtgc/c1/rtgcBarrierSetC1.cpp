
#include "precompiled.hpp"
#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"

void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  if (!access.is_oop()) {
    BarrierSetC1::store_at_resolved(access, value);
    return;
  }

  LIRGenerator* gen = access.gen();
  LIR_Opr base = access.base().opr();
  LIR_Opr addr = access.resolved_addr()->as_address_ptr()->base();

  BasicTypeList signature;
  signature.append(T_OBJECT);    // object
  signature.append(T_INT); // offset
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base);
  args->append(addr);
  args->append(value);

  gen->call_runtime(&signature, args,
              CAST_FROM_FN_PTR(address, RTGC::RTGC_StoreObjField),
              objectType, NULL);
  return;    
}


LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  if (!access.is_oop()) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  LIRGenerator* gen = access.gen();
  LIR_Opr base = access.base().opr();
  LIR_Opr addr = access.resolved_addr()->as_address_ptr()->base();
  cmp_value.load_item();
  new_value.load_item();

  BasicTypeList signature;
  signature.append(T_OBJECT);    // object
  signature.append(T_INT); // offset
  signature.append(T_OBJECT); // comapre_value
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base);
  args->append(addr);
  args->append(cmp_value.result());
  args->append(new_value.result());

  LIR_Opr res = gen->call_runtime(&signature, args,
              CAST_FROM_FN_PTR(address, RTGC::RTGC_CmpXchgObjField),
              objectType, NULL);
  return res;    
}

LIR_Opr RtgcBarrierSetC1::atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
  if (!access.is_oop()) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  LIRGenerator* gen = access.gen();
  LIR_Opr base = access.base().opr();
  LIR_Opr addr = access.resolved_addr()->as_address_ptr()->base();
  value.load_item();

  BasicTypeList signature;
  signature.append(T_OBJECT);    // object
  signature.append(T_INT); // offset
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base);
  args->append(addr);
  args->append(value.result());

  LIR_Opr res = gen->call_runtime(&signature, args,
              CAST_FROM_FN_PTR(address, RTGC::RTGC_StoreObjField),
              objectType, NULL);
  return res;    
}
