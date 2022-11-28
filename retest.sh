#         --add-modules jdk.compiler \
#        --add-modules java.base \
#        --add-exports jdk.compiler/com.sun.tools.javac.api=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.javac.util=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.javac.file=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.javac.main=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac.server=ALL-UNNAMED \
#        -XX:+UnlockExperimentalVMOptions \
#        -XX:AbortVMOnExceptionMessage='#[B' \
#        -XX:AbortVMOnExceptionMessage='compiler/c2/Test7190310$1' \
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

#        -XX:+UnlockExperimentalVMOptions \
#        -XX:AbortVMOnExceptionMessage='#' \

pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
CLASSPATH=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0/compiler/codegen/TestOopCmp.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/codegen:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
       -XX:+UnlockExperimentalVMOptions \
       -XX:AbortVMOnExceptionMessage='#' \
        -Dtest.vm.opts='-XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/tmp' \
        -Dtest.tool.vm.opts='-J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/tmp' \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dtest.timeout.factor=4.0 \
        -Dtest.nativepath=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg \
        -Dtest.name=compiler/codegen/TestOopCmp.java \
        -Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/codegen/TestOopCmp.java \
        -Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/codegen \
        -Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/codegen:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg \
        -Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0/compiler/codegen/TestOopCmp.d \
        -Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0/compiler/codegen/TestOopCmp.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0 \
        -Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0/compiler/codegen/TestOopCmp.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/codegen:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/classes/0 \
        -Dtest.modules=java.base/jdk.internal.misc:+open \
        --add-modules java.base \
        --add-exports java.base/jdk.internal.misc=ALL-UNNAMED \
        --add-opens java.base/jdk.internal.misc=ALL-UNNAMED \
        -XX:MaxRAMPercentage=6.25 \
        -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/tmp \
        -Djava.library.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -Xbatch \
        -XX:-UseTLAB \
        -Xmx4m \
        -XX:+UseSerialGC \
        -XX:HeapBaseMinAddress=0x700000000 \
        -XX:CompileCommand=compileonly,compiler.codegen.TestOopCmp::nullTest \
        -XX:+UnlockDiagnosticVMOptions \
        -XX:+WhiteBoxAPI \
        -Xbootclasspath/a:. \
        com.sun.javatest.regtest.agent.MainWrapper /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_codegen_TestOopCmp_java/compiler/codegen/TestOopCmp.d/main.0.jta