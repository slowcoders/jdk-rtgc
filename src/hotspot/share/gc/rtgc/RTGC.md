1. Prepare external libraries

jtreg https://github.com/openjdk/jtreg/tree/jtreg-6.1+1
   git clone https://github.com/openjdk/jtreg.git
   git checkout -b jtreg-6.1+1
   bash build.sh --jdk /usr/lib/jvm/adoptopenjdk-14-hotspot-amd64

-  jtreg https://github.com/openjdk/jtreg/tree/jtreg-6.1+1
   git clone https://github.com/openjdk/jtreg.git
   git checkout -b jtreg-6.1+1
   bash build.sh --jdk /usr/lib/jvm/adoptopenjdk-14-hotspot-amd64
-  google test
   git clone https://github.com/google/googletest.git -b release-1.8.1

2. Run configure
```
bash configure --with-jvm-variants=client \
  --enable-ccache \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=./jtreg-6.1 \
  --with-gtest=./googletest
```
// --with-toolchain-type=clang \

3. Make Images
    `make images CONF=linux debug`
    `make images CONF=macosx debug`

4. precompiled file 삭제
   ` rm -rf ./build/macosx-x86_64-client-fastdebug/hotspot/variant-client/libjvm/objs/precompiled `

5. Run basic tests
   `ulimit -c unlimited; make run-test-tier1 CONF=linux debug`
   `ulimit -c unlimited; make run-test-tier1 CONF=macosx debug`

        new java.util.concurrent.atomic.AtomicLong().compareAndExchange(0x876543DB876543DBL, 0x123456DB123456DBL);

- test codedump 파일 자동삭제 방지<br>
  RunTests.gmk 파일을 아래와 같이 수정. <br>
   -> JTREG_RETAIN ?= fail,error,hs_err_pid*

- coredump file 찾기.
  find build -name "hs_err_pid*"

- coredump file 삭제.<br>
  find build -name "hs_err_pid*" | xargs rm 


make test CONF="macosx" TEST="jtreg:test/hotspot:hotspot_gc:serial"
// implicit null check exception.
make test CONF="macosx" TEST="compiler/c1/Test7103261.java"
make test CONF="macosx" TEST="compiler/c2/Test6910605_1.java"
make test CONF="macosx" \
  TEST="jtreg:test/hotspot:hotspot_gc compiler/gcbarriers/UnsafeIntrinsicsTest.java"

make test CONF="macosx" \
   TEST="jtreg:test/hotspot compiler/c1/Test7103261.java"

6. Test file build
   javac test/rtgc/Main.java

7. Test 실행
   // interpreter only
   ./build/macosx-x86_64-client-fastdebug/images/jdk/bin/java -cp test/rtgc Main 2 100000 
   ./build/linux-x86_64-client-fastdebug/images/jdk/bin/java -cp test/rtgc Main 2 1 
   
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

### null point emplicit check 관련 정리.
RTGC_EXPLICT_NULL_CHCECK_ALWAYS 를 사용하여 임시 조치.<br>
-> 이후 assembly 코드를 분석하여 문제 원인을 정확히 파악
- signal hnadler
   os_bsd_x86.cpp: pd_hotspot_signal_handler
- handler 함수 address 반환
   address CompiledMethod::continuation_for_implicit_exception
   address SharedRuntime::continuation_for_implicit_exception(JavaThread* current,



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

https://code.visualstudio.com/docs/cpp/config-clang-mac
{
  // Use IntelliSense to learn about possible attributes.
  // Hover to view descriptions of existing attributes.
  // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
  "version": "0.2.0",
  "configurations": [
    {
      "name": "clang++ - Build and debug active file",
      "type": "cppdbg",
      "request": "launch",
      "program": "${fileDirname}/${fileBasenameNoExtension}",
      "args": [],
      "stopAtEntry": true,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "lldb",
      "preLaunchTask": "clang++ build active file"
    }
  ]
}



