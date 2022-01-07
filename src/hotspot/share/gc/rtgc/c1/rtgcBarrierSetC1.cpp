
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
#include "runtime/arguments.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vm_version.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/macros.hpp"
#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"
#include "gc/rtgc/rtgc_jrt.hpp"

volatile int enable_rtgc_c1_barrier_hook = 1;
static int getOopShift() {
#ifdef _LP64
  if (UseCompressedOops) {
    assert(0 == CompressedOops::base(), "invalid narrowOop base");
    assert(3 == CompressedOops::shift()
        || 0 == CompressedOops::shift(), "invalid narrowOop shift");
    return CompressedOops::shift();
  }
  else {
    return 8;
  }
#else
  return 4;
#endif
}

void RtgcBarrierSetC1::store_at(LIRAccess& access, LIR_Opr value) {
  if (!enable_rtgc_c1_barrier_hook || !access.is_oop()) {
    BarrierSetC1::store_at(access, value);
    return;
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  LIR_Opr offset = access.offset().opr();//->as_address_ptr()->base();

  assert(access.decorators() & IN_HEAP, "store_at not in heap!!");

  BasicTypeList signature;
  signature.append(T_OBJECT);    // object
  signature.append(T_INT); // offset
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base.result());
  args->append(offset);
  args->append(value);

  address fn;
  if (access.decorators() & IS_ARRAY) {
    // 참고) Index range is already cheked. 
    switch (getOopShift()) {
      case 0: fn = CAST_FROM_FN_PTR(address, rtgc_oop_array_xchg_0); break;
      case 3: fn = CAST_FROM_FN_PTR(address, rtgc_oop_array_xchg_3); break;
      case 8: fn = CAST_FROM_FN_PTR(address, rtgc_oop_array_xchg_8); break;
      default: assert(false, "invalid oop shift");
    }
  }
  else {
    switch (getOopShift()) {
      case 0: fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_0); break;
      case 3: fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_3); break;
      case 8: fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_8); break;
      default: assert(false, "invalid oop shift");
    }
  }
  gen->call_runtime(&signature, args,
              fn,
              voidType, NULL);
  return;    
}


LIR_Opr RtgcBarrierSetC1::atomic_xchg_at(LIRAccess& access, LIRItem& value) {
  if (!enable_rtgc_c1_barrier_hook || !access.is_oop()) {
    return BarrierSetC1::atomic_xchg_at(access, value);
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  LIRItem offset = access.offset().item();
  base.load_item_force(FrameMap::as_oop_opr(c_rarg0));
  offset.load_item_force(FrameMap::as_opr(c_rarg1));
  value.load_item_force(FrameMap::as_oop_opr(c_rarg2));
      // new_value.load_item_force(FrameMap::as_oop_opr(j_rarg3));
  assert(access.decorators() & IN_HEAP, "store_at not in heap!!");

  BasicTypeList signature;
  signature.append(T_OBJECT);    // object
  signature.append(T_INT); // offset
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base.result());
  args->append(offset.result());
  args->append(value.result());

  address fn;
  switch (getOopShift()) {
    case 0: fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_0); break;
    case 3: fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_3); break;
    case 8: fn = CAST_FROM_FN_PTR(address, rtgc_oop_xchg_8); break;
    default: assert(false, "invalid oop shift");
  }
  LIR_Opr res = gen->call_runtime(&signature, args,
                fn,
                objectType, NULL);
  return res;    
}


LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  if (!enable_rtgc_c1_barrier_hook || !access.is_oop()) {
    return BarrierSetC1::atomic_cmpxchg_at(access, cmp_value, new_value);
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  LIRItem offset = access.offset().item();
  base.load_item_force(FrameMap::as_oop_opr(c_rarg0));
  offset.load_item_force(FrameMap::as_opr(c_rarg1));
  cmp_value.load_item_force(FrameMap::as_oop_opr(c_rarg2));
  new_value.load_item_force(FrameMap::as_oop_opr(c_rarg3));
  assert(access.decorators() & IN_HEAP, "store_at not in heap!!");


  BasicTypeList signature;
  signature.append(T_OBJECT); // object
  signature.append(T_INT);    // offset
  signature.append(T_OBJECT); // cmp_value
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  args->append(base.result());
  args->append(offset.result());
  args->append(cmp_value.result());
  args->append(new_value.result());

  address fn;
  switch (getOopShift()) {
    case 0: fn = CAST_FROM_FN_PTR(address, rtgc_oop_cmpxchg_0); break;
    case 3: fn = CAST_FROM_FN_PTR(address, rtgc_oop_cmpxchg_3); break;
    case 8: fn = CAST_FROM_FN_PTR(address, rtgc_oop_cmpxchg_8); break;
    default: assert(false, "invalid oop shift");
  }
  LIR_Opr res = gen->call_runtime(&signature, args,
              fn,
              objectType, NULL);
  return res;    
}

