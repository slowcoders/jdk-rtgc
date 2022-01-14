

 2. [Run configure](#running-configure): \
    `bash configure`

bash configure --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=/workspaces/jdk-rtgc/jtreg-6.0

bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=/workspaces/jdk-rtgc/jtreg-6.0

 3. [Run make](#running-make): \
    `make images CONF=client`

 4. Verify your newly built JDK: \
    `./build/*/images/jdk/bin/java -version`

 5. [Run basic tests](##running-tests): \
    `make run-test-tier1`


6. Test file build
   javac test/rtgc/Main.java

7. Test 실행
   // interpreter only
   ./build/linux-x86_64-client-fastdebug/images/jdk/bin/java -XX:+UnlockExperimentalVMOptions -XX:+UseRTGC -cp test/rtgc Main 2 1 
   
   // enable c1_LIRGenerator 
   ./build/linux-x86_64-client-fastdebug/images/jdk/bin/java -XX:+UnlockExperimentalVMOptions -XX:+UseRTGC -cp test/rtgc Main 2 1000 

8. Debugging 
  .vscode/launch.json "Launch Main" 실행.
  ENABLE_RTGC_STORE_HOOK = 1 (RTGC_HOOK enable)
  ENABLE_RTGC_STORE_TEST = 1 ( TEST Log 출력)


9. arraycopy 의 추가적인 최적화
LIR_OpArrayCopy::emit_code() 
   //c1_LIRAssembler_x86.cpp 
   -> LIR_Assembler::emit_arraycopy(LIR_OpArrayCopy* op) 
      StubRoutines::select_arraycopy_function() 함수를 통해
         stubGenerator_x86_64.cpp 의
            generate_*****_int_oop_copy 등에 의해 생성된 함수 호출


10. JVM SetField
- putfield
- putstatic
- clone() -> JVM_Clone()
- arraycopy()
- Unsafe::PutReference
- Unsafe::CompareAndSetReference
- Unsafe::ComapreAndExchageReference
- JNI jni_SetObjectField jni_SetObjectArrayElement


RuntimeDispatch<decorators,..> 를 이용하여 모드 Hook을 처리??

CollectedHeap 을 상속한 genCollectedHeap 가 default 선택
genCollectedHeap->initialize() 에서 CardTableBarrierSet 을 생성.
   BarrierSet::set_barrier_set() 호출.

## What is a Safepoint ?
A safepoint is a state of your application execution where all references to objects are perfectly reachable by the VM.

Some operations of the VM require that all threads reach a safepoint to be performed. The most common operation that need it is the GC.

A safepoint means that all threads need to reach a certain point of execution before they are stopped. Then the VM operation is performed. After that all threads are resumed.


oopDesc::markWord (markWord.hpp)
하위 3bit 값이,
   monitor_value(0x2) 인 경우, ObjectMonitor*
      = v ^ monitor_value 
      --> 원본 markWord 갑 = (markWord*)(ObjectMonitor*)v;
   biased_lock_pattern(0x5) 인 경우, JavaThread* + age + epoc
      = v & ~(biased_lock_mask_in_place | age_mask_in_place | epoch_mask_in_place)
   locked_value(0x0) 인 경우, (BasicLock*)
      = v

### psMarkSweep.cpp:501
```
void PSMarkSweep::mark_sweep_phase1(bool clear_all_softrefs) {
   ...
    Universe::oops_do(mark_and_push_closure());
    JNIHandles::oops_do(mark_and_push_closure());   // Global (strong) JNI handles
    MarkingCodeBlobClosure each_active_code_blob(mark_and_push_closure(), !CodeBlobToOopClosure::FixRelocations);
    Threads::oops_do(mark_and_push_closure(), &each_active_code_blob);
    ...
```

Generation 구조
Generation
   -> DefNewGenration (Single Space??)
   -> CardGenaration (다수의 Sapce??.)
      -> TenuredGeneration

### SerialGC 처리 과정.
```
void GenCollectedHeap::collect_generation()
   -> TenuredGeneration::collect // old gen 
   or DefNewGenration::collect // young gen
      -> GenMarkSweep::invoke_at_safepoint()
         -> GenMarkSweep::mark_sweep_phase1()
            -> GenCollectedHeap::full_process_roots
               -> GenCollectedHeap::process_roots
         -> GenMarkSweep::mark_sweep_phase2()
            -> GenCollectedHeap::prepare_for_compaction()
               -> Generation::prepare_for_compaction(CompactPoint* cp) {
                  -> for_each Space::prepare_for_compaction
                     -> CompactibleSpace::scan_and_forward() 
```

### CollectedHeap::collect()
```
   GenCollectedHeap::collect(GCCause::Cause cause) {
      ...> collect_generation(...)
   }

   ParallelScavengeHeap::collect(GCCause::Cause cause) {
      if (UseParallelOldGC) {
         PSParallelCompact::invoke(maximum_compaction);
      } else {
         PSMarkSweep::invoke(clear_all_soft_refs);
      }
   }

   G1CollectedHeap::collect(GCCause::Cause cause) {
      G1CollectedHeap::try_collect_concurrently() {
         VM_G1TryInitiateConcMark op(gc_counter, cause, ..);
         VMThread::execute(&op);   
      }
   }

   ShenandoahHeap::collect(GCCause::Cause cause) {
      control_thread()->request_gc()
   }

   void ZCollectedHeap::collect(GCCause::Cause cause) {
      ZDriveer::Collect()
   }
```   