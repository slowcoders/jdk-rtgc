## TODO
- Old-G 에 객체 A를 allocate 하고, 바로 full-GC 수행 시, A 의 trackable-marking 여부 확인!!
  -> rtHeap::adjust_points 을 통해 처리.
- FinalReference 의 referent 는 rootRefCount 만 증가시킨다. 
  즉, rootRefCount 가 1인 final-referent 는 garbage.
- YoungRoot resurrection.
- lock temporary WeakHandles. (YG GC 시에는 GC 되지 않음).
- SafepointSynchronize::begin()/end()확인.
- SafepointSynchronize::arm_safepoint() 분석
- YG 객체에 의한 Old 객체 참조 문제.
   1) YG 객체도 AnchorList를 생성한다. - 데이터 소비 및 속도 문제.
   2) YG 객체에 의한 참조는 Ref-Count 변경 - YG Garbage 객체에 대한 추가적 Scan 필요 (Old 객체에 대한 refCount 감소를 위해).
   3) YG 객체에 의한 참조 시 unmarkGarbage 실행.
- Reference 처리 문제
   1) Reference 를 referent 의 anchor 로 등록하면,
      referent 만 따로 가비지 처리하지 못한다.
      - 단, YG GC 시에는 문제가 없다.
      - Full GC 시에는 각 Reference 의 referent 별로, Reference 를 제외한 Anchor 의 수를 세어
        Garbage 검사를 할 수 있다.
      - FinalizeReference(=Finalizer) 의 경우, 별도의 List-Q 를 통해 저장된다. 이에 매우 긴 SafeShorcut 이 생성된다.
      - PhantomReference 는 존속기간을 늘리는 문제가 발생.
   2) Reference 를 referent 의 anchor 로 등록하지 않으면,
      referent 값을 지우지 않은 채 referent 가 삭제될 수 있다.
      이를 방지하려면, stringRefCount 와 별도로 weakRefCount 가 관리되어야 한다.
      (stringRefCount 하나로 관리하면, 순환 가비지 처리 불가)
      softRefCount 도 별도 관리 필요??? ㅡ,.ㅡ
   3) GC Root marking 시에, 가비지 처리 대상이 아닌 referent 를 stack-root-marking 한다.
      YG-GC 시에는 reference 를 일반 객체와 동일하게 처리하므로, referent 가 별도 GC 되지 않는다.
         referent 만 old-G 에 allocation 되거나, forwaring 된 경우에 대해 확인 필요!!
      Full-GC 시에 WeakReferent 는 항상 별도 GC 대상이므로 stack-root-marking 이 필요없다.
      SoftReference 에 한해서 별도 관리가 필요하다. (1차 Full-GC 후, SoftReference 를 재처리하는 경우도 대비.)
        -> SoftReference 를 일반 anchor 로 등록한 후, 
           soft-referent 의 anchor 중 SoftReference 를 제외한 anchor 의 수를 세어 GC 여부 재판별 가능.
           soft-referent 가 가비지로 판별되면 그 ref-link 에 대해서도 동일 조건으로 GC 여부 재판별 가능.
      FinalizeReference 에 대한 resurrection 처리 또한 필요하다.
         -> 필드 변경 시마다 resurrection 을 하기 보다는 finalize 처리 전에 resurrection 을 일괄 처리(?)
         -> finalizable 객체가 가비지로 판별 시, 해당 객체를 가비지 markging 하지 않고, finalizeQueue 에만 push!


      

## RTGC 1차 구현
1. Ref Counting 방식의 단점.
   가비지 제거 전에 refCount 및 refSet 을 update 하는 과정이 필요하다.
   refSet 은 Compact GC 에 적합하지 않다. 
   - 다량의 객체가 빈번하게 가비지로 변경되는 Younger Generation 은 TLAB + Compact-GC 가 유리
2. Old-Generation 에 대해서만 RTGC 적용.
   소량의 객체가 빈번하게 가비지로 변경되는 Older Generation 에 RTGC 가 적합.
   RefLink 관리 부담 감소  
3. Compact GC Overhead 처리
   별도 Mem-Manager 구현?
3. YG의 root가 되는 old 객체에 대한 Garbage 여부를 판별하여 YG GC 효율성 높이기 
   Age 가 MinAge 이상인 YG 객체에 대한 GC 판별
     

## Mark & Compact GC의 장점.
   소량의 객체가 Strong-reachable 상태인 경우, 즉 Younger Generation에 적합.
   TLAB 활용성 극대화.


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
    `make images CONF=linux debug LOG_LVEL=info`
    `make images CONF=macosx debug LOG_LVEL=info`

## 4. Run basic tests
   `ulimit -c unlimited; make run-test-tier1 CONF=linux debug`
   `ulimit -c unlimited; make run-test-tier1 CONF=macosx debug`

## 5. Test tips
- test codedump 파일 자동삭제 방지<br>
  RunTests.gmk 파일을 아래와 같이 수정. <br>
```  
   -> JTREG_RETAIN ?= fail,error,hs_err_pid*
```

- precompiled file 삭제
```sh 
   rm -rf ./build/macosx-x86_64-client-fastdebug/hotspot/variant-client/libjvm/objs/precompiled 
```

- coredump file 찾기.
  find build -name "hs_err_pid*"

- coredump file 삭제.<br>
  find build -name "hs_err_pid*" | xargs rm 


- logTrigger
```
   new java.util.concurrent.atomic.AtomicLong().compareAndExchange(
      0x876543DB876543DBL, 0x123456DB00000000L + (category << 24) + functions));` 
```

## 5. Tests
- new RTGC error
```
   make test CONF="macosx" TEST="jdk/jfr/event/gc/collection/TestGCGarbageCollectionEvent.java"  
   make test CONF="macosx" TEST="runtime/Monitor/SyncOnValueBasedClassTest.java"
```

- narrowOop shift test (0,1,2,3,4)
```
   make test CONF="macosx" TEST="gc/arguments/TestUseCompressedOopsErgo.java"
```
   

- implicit null check exception.
```
   make test CONF="macosx" TEST="compiler/c1/Test7103261.java"
```

- unsafeAccess
```
   make test CONF="macosx" TEST=" \
      compiler/unsafe/SunMiscUnsafeAccessTestObject.java \
      compiler/unsafe/JdkInternalMiscUnsafeAccessTestObject.java "
```

- Huge object (size > 256K)
```
   make test CONF="macosx" TEST="gc/g1/plab/TestPLABPromotion.java"
   make test CONF="macosx" TEST="gc/g1/plab/TestPLABResize.java"
```

- Single stack check
```
   make test CONF="macosx" TEST="compiler/c2/Test6910605_1.java"
```

6. Test file build
   javac test/rtgc/Main.java

7. Custom Test 실행
export CLASSPATH=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_jdk_jfr_event_gc_collection_TestGCGarbageCollectionEvent_java/classes/0/jdk/jfr/event/gc/collection/TestGCGarbageCollectionEvent.d:/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/jdk/jfr/event/gc/collection:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_jdk_jfr_event_gc_collection_TestGCGarbageCollectionEvent_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar

- macosx
   ./build/macosx-x86_64-client-fastdebug/images/jdk/bin/java -XX:+UnlockExperimentalVMOptions -Xlog:gc=trace -cp test/rtgc Main 200 100000
- linux
   ./build/linux-x86_64-client-fastdebug/images/jdk/bin/java -XX:+UnlockExperimentalVMOptions -Xlog:gc=trace -cp test/rtgc Main 200 100000
   
   // enable c1_LIRGenerator 
   ./build/linux-x86_64-client-fastdebug/images/jdk/bin/java -XX:+UnlockExperimentalVMOptions -XX:+UseRTGC -cp test/rtgc Main 2 1000 

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


