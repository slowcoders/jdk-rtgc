
## 1. Prepare external libraries
-  jtreg 
```sh
   git clone https://github.com/openjdk/jtreg.git -b jtreg-6.1+1
   bash build.sh --jdk /usr/lib/jvm/adoptopenjdk-14-hotspot-amd64
```

-  google test (소스만 필요)
```sh
   git clone https://github.com/google/googletest.git -b release-1.8.1
```
- ccache 설치
```sh
   brew install ccache
```
- VS Code C++ in Macosx
   https://code.visualstudio.com/docs/cpp/config-clang-mac

## 2. Run configure
* for macosx
```
bash configure --with-jvm-variants=client \
  --enable-ccache \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=./jtreg-6.1 \
  --with-gtest=./googletest

  --with-toolchain-type=clang \
```
* for linux container
```
bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=./jtreg-6.1 \
  --with-gtest=./googletest
```

## 3. Make Images
    `make images CONF=linux debug LOG_LEVEL=info`
    `make images CONF=macosx debug LOG_LEVEL=info`

## 4. Run basic tests
   `ulimit -c unlimited; make run-test-tier1 CONF=linux debug`
   `ulimit -c unlimited; make run-test-tier1 CONF=macosx debug`

   `make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/jdk:tier1`
   `make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/langtools:tier1`
   `make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/hotspot/jtreg:tier1`

## 5. Test tips
- test codedump 파일 자동삭제 방지<br>
  RunTests.gmk 파일을 아래와 같이 수정. <br>
```  
   -> JTREG_RETAIN ?= fail,error,hs_err_pid*
```

- logTrigger
```
   new java.util.concurrent.atomic.AtomicLong().compareAndExchange(
      0x876543DB876543DBL, 0x123456DB00000000L + (category << 24) + functions));` 
```

- module jdk.compiler does not export com.sun.tools.javac.xxx to unnamed module 오류 해결 방법
```
      --add-opens=jdk.compiler/com.sun.tools.javac.xxx=ALL-UNNAMED \
```

- retest.sh 실행 시 WhiteBox jni link error 발생 시, 아래 스크립트를 먼저 실행.
```
   rerun: ... 
      jdk.test.lib.helpers.ClassFileInstaller sun.hotspot.WhiteBox
```

- retest.sh 실행 시 log 가 출력되지 않으면, inheritIO() 추가.
```
   ProcessBuiler.inheritIO().start();
```

6. Test file build
   javac test/rtgc/Main.java

7. Custom Test 실행
   ./build/macosx-x86_64-client-fastdebug/images/jdk/bin/java -Xlog:gc=trace -cp test/rtgc Main 200 100000

8. Debugging 
  .vscode/launch.json "Launch Main" 실행.
  ENABLE_RTGC_STORE_HOOK = 1 (RTGC_HOOK enable)
  ENABLE_RTGC_STORE_TEST = 1 ( TEST Log 출력)

9. JVM SetField
- putfield
- putstatic
- clone() -> JVM_Clone()
- arraycopy()
- Unsafe::PutReference
- Unsafe::CompareAndSetReference
- Unsafe::ComapreAndExchageReference
- JNI jni_SetObjectField jni_SetObjectArrayElement

### null point emplicit check 관련 정리.
RtExplictNullCheckAlways 를 사용하여 임시 조치.<br>
-> 이후 assembly 코드를 분석하여 문제 원인을 정확히 파악
- signal hnadler
   os_bsd_x86.cpp: pd_hotspot_signal_handler
- handler 함수 address 반환
   address CompiledMethod::continuation_for_implicit_exception
   address SharedRuntime::continuation_for_implicit_exception(JavaThread* current,

## ON_UNKNOWN_OOP_REF 처리
DecoratorSet AccessBarrierSupport::resolve_unknown_oop_ref_strength(
   DecoratorSet decorators, oop base, ptrdiff_t offset);


## Safepoint 
A safepoint is a state of your application execution where all references to objects are perfectly reachable by the VM.
- 함수 종료 시, Loop 반복 시, 
특정 메모리 영역을 read 하고, SafepointException에 의해 safePoint 진입.

## markWord
oopDesc::markWord (markWord.hpp)
하위 3bit 값이,
   monitor_value(0x2) 인 경우, ObjectMonitor*
      = v ^ monitor_value 
      --> 원본 markWord 값 = (markWord*)(ObjectMonitor*)v;
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
   -> DefNewGeneration::collect // young gen
         SerialHeap::young_process_roots 
            // younger_gen 중의 root(stack또는 old_gen에 의 해 참조) 만 old_gen 으로 이동.
            GenCollectedHeap::process_roots
               Threads::oops_do(strong_roots, roots_from_code_p)
                  Thread::oops_do(f, cf)
                     JavaThread::oops_do_no_frames(OopClosure* f, CodeBlobClosure* cf)
                     JavaThread::oops_do_frames(OopClosure* f, CodeBlobClosure* cf)
                        frame::oops_do()
            old_gen()->younger_refs_iterate(old_gen_closure);
         FastEvacuateFollowersClosure::do_void() -> 나머지 younger_gen 이동.

   or TenuredGeneration::collect // old gen 
      -> GenMarkSweep::invoke_at_safepoint()
         -> GenMarkSweep::mark_sweep_phase1()
            -> GenCollectedHeap::full_process_roots
               -> GenCollectedHeap::process_roots
         -> GenMarkSweep::mark_sweep_phase2()
            -> GenCollectedHeap::prepare_for_compaction()
               -> Generation::prepare_for_compaction(CompactPoint* cp) {
                  -> for_each Space::prepare_for_compaction
                     -> CompactibleSpace::scan_and_forward() 
         -> GenMarkSweep::mark_sweep_phase3
            ... -> MarkSweep::adjust_pointers() -> marking 된 forwarded ref 처리. 
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




* MemAllocator
void LIR_List::allocate_object(LIR_Opr dst..) 
   solw_path = new NewInstanceStub(klass_reg, Runtime1::fast_new_instance_id : ..)
   append(new LIR_OpAllocObj..., slow_path = 
   void LIR_Assembler::emit_alloc_obj(LIR_OpAllocObj* op, slow_path)
      void C1_MacroAssembler::allocate_object(..,, slow_path)
         C1_MacroAssembler::try_allocate(... , slow_path)
      
      NewInstanceStub with Runtime1::fast_new_instance_id
         JVMCIRuntime::new_instance
            JVMCIRuntime::new_instance_common
               InstanceKlass::allocate_instance !!!

void TemplateTable::_new()
   BarrierSetAssembler::tlab_allocate(.., slow_path=InterpreterRuntime::_new) 
   and jni alloc functions...
   InterpreterRuntime::_new   
      klass->allocate_instance !!!
         Universe::heap()->obj_allocate  
            ObjAllocator.allocate()
            = oop MemAllocator::allocate();
               1. HeapWord* MemAllocator::allocate_inside_tlab()
                  HeapWord* mem = _thread->tlab().allocate(_word_size);
                  if (mem != NULL) {
                     return mem;
                  }
                  HeapWord* MemAllocator::allocate_inside_tlab_slow() 
                     -> Universe::heap()->allocate_new_tlab()
               2. MemAllocator::allocate_outside_tlab
                  GenCollectedHeap::mem_allocate
                     GenCollectedHeap::mem_allocate_work
                        try yong->par_allocate()
                        try GenCollectedHeap::attempt_allocation()
                        try GenCollectedHeap::expand_heap_and_allocate()
                        VM_GenCollectForAllocation
                           GenCollectedHeap::satisfy_failed_allocation()
                              GenCollectedHeap::do_collection

* displaced mark_helper
markWord markWord::displaced_mark_helper()
   ObjectMonitor, BasicLock 은 별도로 markWord 보관
   must_be_preserved() GC 수행 도중 보관 여부.
   copy_set_hash() 를 사용하는 곳이 3곳 있음. (해당 문제 해결해야 RTGC 사용 가능)
<<중요>>markWord 는 compact phase 에서 이동할 주소를 저장하기 위하여 사용된다.

## Finalizer 처리.
   InstanceKlass::register_finalizer
      if (klass->has_finalizer())
      -> Universe::finalizer_register_method()
            Finalizer(extends FinalReference).register()
               new Finalizer(boj);

## hashCode 생성
   synchronizer.cpp:808 get_next_hash를 이용하여 hashCode 를 생성한다.
   globals.hpp:726 외부 옵션으로 hashCode 생성 방식을 변경할 수 있다. default:5

lock_bits: 2
biased_lock_bits: 1
age_bits: 4
unused_gap_shift : LP64_ONLY(1) NOT_LP64(0);
epoch_bits : 2
hash_bits : max_hash_bits > 31 ? 31 : max_hash_bits;

  static const int age_bits                       = 4;
  static const int lock_bits                      = 2;
  static const int biased_lock_bits               = 1;
  static const int max_hash_bits                  = BitsPerWord - age_bits - lock_bits - biased_lock_bits;
  static const int hash_bits                      = max_hash_bits > 31 ? 31 : max_hash_bits;
  static const int unused_gap_bits                = LP64_ONLY(1) NOT_LP64(0);
  static const int epoch_bits                     = 2;

  // The biased locking code currently requires that the age bits be
  // contiguous to the lock bits.
  static const int lock_shift                     = 0;
  static const int biased_lock_shift              = lock_bits(2);
  static const int age_shift                      = lock_bits(2) + biased_lock_bits(1);
  static const int unused_gap_shift               = age_shift(3) + age_bits(4);
  static const int hash_shift                     = unused_gap_shift(7) + unused_gap_bits(?1:0);
  static const int epoch_shift                    = hash_shift(7+1?);

0x7f992c008a20] RTGC.cpp:346 lock_mask 0x3
0x7f992c008a20] RTGC.cpp:347 lock_mask_in_place 0x3
0x7f992c008a20] RTGC.cpp:348 biased_lock_mask 0x7
0x7f992c008a20] RTGC.cpp:349 biased_lock_mask_in_place 0x7
0x7f992c008a20] RTGC.cpp:350 biased_lock_bit_in_place 0x4
0x7f992c008a20] RTGC.cpp:351 age_mask 0xf
0x7f992c008a20] RTGC.cpp:352 age_mask_in_place 0x78
0x7f992c008a20] RTGC.cpp:353 epoch_mask 0x3
0x7f992c008a20] RTGC.cpp:354 epoch_mask_in_place 0x300
0x7f992c008a20] RTGC.cpp:356 hash_mask 0x7fffffff
0x7f992c008a20] RTGC.cpp:357 hash_mask_in_place 0x7fffffff00
0x7f992c008a20] RTGC.cpp:359 unused_gap_bits 0x1
0x7f992c008a20] RTGC.cpp:360 unused_gap_bits_in_place 0x80

## Reference 처리.
   !!! Referene 가 collect 되면, RefereceQueue 에 등록(enqueue)되지 않는다!
   JVM은 별도의 RefList 를 관리하지 않음. 객체 Marking 시, RefClass 에 대해서 예외처리 함.
   (Reference 자체가 old-gen으로 이동하면, referent 를 marking 하지 못할 수 있음. enqueue 처리가 늦어지는 것이 단점.)
DefNewGeneration::collect() ...
   void InstanceRefKlass::oop_oop_iterate***() {
      void InstanceRefKlass::oop_oop_iterate_ref_processing() {
         void InstanceRefKlass::oop_oop_iterate_discovered_and_discovery() {
         void InstanceRefKlass::oop_oop_iterate_discovery() {
            bool InstanceRefKlass::try_discover() {
               ReferenceDiscoverer* rd = closure->ref_discoverer();
               if (!referent->is_gc_marked()) {
                  // Reference는 marking 되고, Referent 는 marking 되지 않은 경우에만
                  // discover 처리.
                  bool ReferenceProcessor::discover_reference(
                     if (RefDiscoveryPolicy == ReferenceBasedDiscovery &&
                           !is_subject_to_discovery(obj)) {
                        // Reference 가 YG인 경우, referent 도 무조건 marking 한다.
                        // 즉, 일반 객체와 동일. 즉, Reference 가 OldG로 옮겨진 경우에만,
                        // unmarked referent를 가진 Reference를 discovered-list 에 추가한다.
                        // referent 는 계속적인 scanning 과정에서 marked 상태로 변경될 수 있다.
                        // Reference 는 referent 보다 나중에 생성되므로, promotion_fail 이
                        // 발생하지 않는 한 reference 만 old-G 로 옮겨질 가능성은 없다.
                        // promotion_fail 에 대한 예외처리 외에도 referent 가 먼저 marking 되어 
                        // YG에 남아있고, reference 만 old-G 로 옮겨질 가능성에 대한 검토도 필요.
                        return false;
                     }
                     ...
                     list = ReferenceProcessor::get_discovered_list(ReferenceType rt)
                     list->add(dicovered reference 등록)
                        -> Reference->discovered field 이용.


==============================
Test summary 2022 11/04 No-Cross-Check
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1585    20     5 <<
>> jtreg:test/jdk:tier1                               2062  2058     1     3 <<
>> jtreg:test/langtools:tier1                         4215  4201     0    14 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================


==============================
Test summary 2022 10/20 Cross-Check
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1587    19     4 <<
>> jtreg:test/jdk:tier1                               2062  2059     0     3 <<
>> jtreg:test/langtools:tier1                         4215  4201     0    14 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================

==============================
Test summary 2022 10/09
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1582    25     3 <<
>> jtreg:test/jdk:tier1                               2062  2058     0     4 <<
>> jtreg:test/langtools:tier1                         4215  4200     0    15 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================

==============================
Test summary 2022 07/20
==============================
>> jtreg:test/hotspot/jtreg:tier1                     1610  1586    21     3 <<
>> jtreg:test/jdk:tier1                               2062  2055     3     4 <<
>> jtreg:test/langtools:tier1                         4214  4199     0    15 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   

   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1582    24     4 <<
>> jtreg:test/jdk:tier1                               2062  2054     3     5 <<
>> jtreg:test/langtools:tier1                         4215  4205     0    10 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0 

==============================
Test summary Orignal version
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1593    16     1 <<
>> jtreg:test/jdk:tier1                               2062  2048     6     8 <<
>> jtreg:test/langtools:tier1                         4019  3874   136     9 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================


  yg->process_roots(ScanningOption SO_ScavengeCodeCache, 
                OopClosure* strong_roots = DefNewScanClosure,
                CLDClosure* strong_cld_closure = cld_closure, 
                CLDClosure* weak_cld_closure = cld_closure, 
                CodeBlobToOopClosure* code_roots = &MarkingCodeBlobClosure(root_closure,
                                                   CodeBlobToOopClosure::FixRelocations);
                
  old_g->process_roots(ScanningOption GenCollectedHeap::SO_None,
               OopClosure* strong_roots = MarkSweep::FollowRootClosure(root_closure),
               CLDClosure* strong_cld_closure = CLDToOopClosure
               CLDClosure* weak_cld_closure = only_strong_roots ? NULL : cld_closure,
               CodeBlobToOopClosure* code_roots = &MarkingCodeBlobClosure(root_closure, is_adjust_phase)
