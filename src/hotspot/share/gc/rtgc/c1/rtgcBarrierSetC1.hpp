
#ifndef SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP
#define SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP

#include "gc/shared/c1/BarrierSetC1.hpp"

class RtgcBarrierSetC1 : public BarrierSetC1 {
public:
  RtgcBarrierSetC1();
  
protected:
  virtual void load_at_resolved(LIRAccess& access, LIR_Opr result);

  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value);

  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value);

  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value);

  virtual const char* rtcall_name_for_address(address entry);
};

#endif // SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP
