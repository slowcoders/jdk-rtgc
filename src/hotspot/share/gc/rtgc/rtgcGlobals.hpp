#ifndef SHARE_GC_RTGC_RTGCDEBUG_HPP
#define SHARE_GC_RTGC_RTGCDEBUG_HPP

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"

class Klass;

namespace RTGC {
  static const int LOG_BARRIER    = 1;
  static const int LOG_BARRIER_C1 = 2;
  static const int LOG_HEAP       = 3;
  static const int LOG_REF_LINK   = 4;
  static const int LOG_GCNODE     = 5;
  static const int LOG_SCANNER    = 6;
  static const int LOG_SHORTCUT   = 7;
  static const int LOG_REF        = 8;
  static const int LOG_TLS        = 9;
  static const int LOG_CLD        = 10;
  static const int LOG_SPACE      = 11;

  static const bool USE_UPDATE_LOG_ONLY = false;
  static const bool ENABLE_BARRIER_LOCK = false || !USE_UPDATE_LOG_ONLY;

  extern Klass* g_dead_array_klass;
  extern Klass* g_dead_object_klass;
  extern int g_cnt_update;
  extern int g_cnt_update_log;
};

#endif // SHARE_GC_RTGC_RTGCDEBUG_HPP
