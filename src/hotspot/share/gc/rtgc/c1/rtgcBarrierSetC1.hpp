
#ifndef SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP
#define SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP

#include "gc/shared/c1/BarrierSetC1.hpp"

class RtgcBarrierSetC1 : public BarrierSetC1 {
protected:

  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value);

  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value);

  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value);
};

#endif // SHARE_GC_RTGC_C1_RTGCBARRIERSETC1_HPP
