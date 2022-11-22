#ifndef SHARE_GC_RTGC_RTGCDEBUG_HPP
#define SHARE_GC_RTGC_RTGCDEBUG_HPP

#include "rtgcLog.hpp"

namespace RTGC {
  static const int LOG_BARRIER    = 1;
  static const int LOG_BARRIER_C1 = 2;
  static const int LOG_HEAP       = 3;
  static const int LOG_REF_LINK   = 4;
  static const int LOG_GCNODE     = 5;
  static const int LOG_SCANNER    = 6;
  static const int LOG_SHORTCUT   = 7;
  static const int LOG_REF        = 8;
};

#endif // SHARE_GC_RTGC_RTGCDEBUG_HPP
