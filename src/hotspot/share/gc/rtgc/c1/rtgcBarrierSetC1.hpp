
#ifndef SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP
#define SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP

#include "gc/shared/c1/barrierSetC1.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"

class RtgcBarrierSetC1 : public BarrierSetC1 {
public:
  RtgcBarrierSetC1();

  static bool needBarrier_onResolvedAddress(LIRAccess& access, bool op_store);

protected:
  virtual void load_at_resolved(LIRAccess& access, LIR_Opr result);

  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value);

  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value);

  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value);

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register);

  virtual const char* rtcall_name_for_address(address entry);
};

#endif // SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP
