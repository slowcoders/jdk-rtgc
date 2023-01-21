#         --add-modules jdk.compiler \
#        --add-modules java.base \
#        --add-exports jdk.compiler/com.sun.tools.javac.api=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.javac.util=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.javac.file=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.javac.main=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac.server=ALL-UNNAMED \
#        --patch-module java.base=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_gc_logging_TestMetaSpaceLog_java/patches/java.base \

# runtime/ErrorHandling/TestGZippedHeapDumpOnOutOfMemoryError
# runtime/ErrorHandling/TestHeapDumpPath
# runtime/Unsafe/InternalErrorTest
# runtime/modules/ModuleStress/ModuleStress
# runtime/modules/ClassLoaderNoUnnamedModuleTest
# gc/logging/TestMetaSpaceLog
# compiler/classUnloading/methodUnloading/TestOverloadCompileQueues
# runtime/logging/VtablesTest
# compiler/intrinsics/bigInteger/TestMulAdd
# gc/logging/TestMetaSpaceLog
# jdk/internal/reflect/CallerSensitive/CheckCSMs
# jdk/java/math/BigInteger/largeMemory/SymmetricRangeTests

# sh exec_test.sh runtime/ErrorHandling/TestHeapDumpPath
#  assert(Universe::is_in_heap(result)) failed: object not in heap 0x00000007fffffff8

# sh exec_test.sh runtime/HiddenClasses/GCHiddenClass
#  assert(UseG1GC) failed: Only possible with a concurrent marking collector

# sh exec_test.sh runtime/modules/ClassLoaderNoUnnamedModuleTest
#  guarantee(java_lang_Module::is_instance(module)) failed: The unnamed module for ClassLoader ClassLoaderNoUnnamedModule$TestClass, is null or not an instance of java.lang.Module. The class loader has not been initialized correctly.

# sh exec_test.sh runtime/logging/RedefineClasses
#  assert(Universe::is_in_heap(result)) failed: object not in heap 0x00000007fffffff8

# sh exec_test.sh gc/TestSoftReferencesBehaviorOnOOME
#  assert(Universe::is_in_heap(result)) failed: object not in heap 0x00000007f8000000

# sh exec_test.sh jdk/jshell/AnalyzeSnippetTest
# sh exec_test.sh runtime/modules/ModulesSymLink
# sh exec_test.sh jdk/jshell/ToolLocalSimpleTest
# sh exec_test.sh runtime/ClassUnload/DictionaryDependsTest
# sh exec_test.sh java/lang/invoke/defineHiddenClass/UnloadingTest

# sh exec_test.sh  compiler/codegen/C1NullCheckOfNullStore #  assert(UseCompressedOops) failed: should be compressed
# sh exec_test.sh  gc/TestAllocateHeapAtMultiple #  assert(UseCompressedOops) failed: should be compressed
# sh exec_test.sh  runtime/CompressedOops/CompressedClassPointers #  assert(UseCompressedOops) failed: should be compressed

# sh exec_test.sh  compiler/c2/Test8007722 # atomix_exchange !!
# sh exec_test.sh  compiler/unsafe/JdkInternalMiscUnsafeAccessTestObject # atomix_exchange !!
# sh exec_test.sh  compiler/unsafe/SunMiscUnsafeAccessTestObject # atomix_exchange !!
# sh exec_test.sh  runtime/Safepoint/TestAbortOnVMOperationTimeout #  assert(oldValue != newValue) -> updateLog allocation failed

# sh exec_test.sh  gc/metaspace/TestMetaspacePerfCounters_id0 #  assert(Universe::heap()->is_in_or_null(r)) failed: bad receiver: 0x000000011d138540 (4782785856)
# sh exec_test.sh  compiler/blackhole/BlackholeIntrinsicTest #  assert(offset != 0) failed: precond
# pass! sh exec_test.sh runtime/modules/ModulesSymLink  # is_modified (array_item)


#        "-XX:+UnlockExperimentalVMOptions",
#        "-XX:AbortVMOnExceptionMessage=#",

#        -XX:+UnlockExperimentalVMOptions \
#        -XX:AbortVMOnExceptionMessage='#' \

pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/javac \
        -J-Xmx768m \
        -J-XX:MaxRAMPercentage=6.25 \
        -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/tmp \
        -J-ea \
        -J-esa \
        -J--add-modules \
        -Jjava.compiler,jdk.compiler \
        -J-Dtest.vm.opts='-Xmx768m -XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/tmp -ea -esa' \
        -J-Dtest.tool.vm.opts='-J-Xmx768m -J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/tmp -J-ea -J-esa' \
        -J-Dtest.compiler.opts= \
        -J-Dtest.java.opts= \
        -J-Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -J-Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -J-Dtest.timeout.factor=4.0 \
        -J-Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools \
        -J-Dtest.name=tools/javac/processing/rounds/MethodsDroppedBetweenRounds.java \
        -J-Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/javac/processing/rounds/MethodsDroppedBetweenRounds.java \
        -J-Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/javac/processing/rounds \
        -J-Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/javac/processing/rounds:/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/javac/lib \
        -J-Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/classes/0/tools/javac/processing/rounds/MethodsDroppedBetweenRounds.d \
        -J-Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/classes/0/tools/javac/processing/rounds/MethodsDroppedBetweenRounds.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/classes/0/tools/javac/lib \
        -J-Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/classes/0/tools/javac/processing/rounds/MethodsDroppedBetweenRounds.d:/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/javac/processing/rounds:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/classes/0/tools/javac/lib \
        -J-Dtest.modules='java.compiler jdk.compiler' \
        @/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_javac_processing_rounds_MethodsDroppedBetweenRounds_java/tools/javac/processing/rounds/MethodsDroppedBetweenRounds.d/compile.0.jta