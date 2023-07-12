#ifndef SHARE_GC_RT_RT_GLOBALS_HPP
#define SHARE_GC_RT_RT_GLOBALS_HPP

#define GC_RT_FLAGS(develop,                                                \
                   develop_pd,                                              \
                   product,                                                 \
                   product_pd,                                              \
                   notproduct,                                              \
                   range,                                                   \
                   constraint)                                              \
                                                                            \
  develop(bool, EnableRTGC, true,                                           \
          "Enable Reverse Tracking garbage collection(RTGC) method")        \
                                                                            \
  develop(bool, RtNoDiscoverPhantom, true,                                  \
          "Disable discovering of phantom reference")                       \
                                                                            \
  develop(bool, RtNoDirtyCardMarking, true,                                 \
          "Disable dirty card marking")


const bool RtLateClearGcMark = true;        
const bool RtExplictNullCheckAlways = true;

// end of GC_RT_FLAGS

#endif // SHARE_GC_RT_RT_GLOBALS_HPP
