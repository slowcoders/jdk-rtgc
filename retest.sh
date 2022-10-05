#         --add-modules jdk.compiler \
#        --add-modules java.base \
#        --add-exports jdk.compiler/com.sun.tools.javac.main=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac.server=ALL-UNNAMED \
#       -XX:+UnlockExperimentalVMOptions \
#       -XX:AbortVMOnExceptionMessage='compiler/c2/Test7190310$1' \
#        -XX:AbortVMOnExceptionMessage='[B' \
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

# runtime/ErrorHandling/TestHeapDumpPath
#  assert(Universe::is_in_heap(result)) failed: object not in heap 0x00000007fffffff8

# runtime/HiddenClasses/GCHiddenClass
#  assert(UseG1GC) failed: Only possible with a concurrent marking collector

# runtime/modules/ClassLoaderNoUnnamedModuleTest
#  guarantee(java_lang_Module::is_instance(module)) failed: The unnamed module for ClassLoader ClassLoaderNoUnnamedModule$TestClass, is null or not an instance of java.lang.Module. The class loader has not been initialized correctly.

# runtime/logging/RedefineClasses
#  assert(Universe::is_in_heap(result)) failed: object not in heap 0x00000007fffffff8

# gc/TestSoftReferencesBehaviorOnOOME
#  assert(Universe::is_in_heap(result)) failed: object not in heap 0x00000007f8000000

pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/javac \
        -J-XX:MaxRAMPercentage=6.25 \
        -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/tmp \
        -J-Djava.library.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -J-Dtest.vm.opts='-XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/tmp' \
        -J-Dtest.tool.vm.opts='-J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/tmp' \
        -J-Dtest.compiler.opts= \
        -J-Dtest.java.opts= \
        -J-Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -J-Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -J-Dtest.timeout.factor=4.0 \
        -J-Dtest.nativepath=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -J-Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg \
        -J-Dtest.name=runtime/HiddenClasses/GCHiddenClass.java \
        -J-Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses/GCHiddenClass.java \
        -J-Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses \
        -J-Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses:/Users/zeedh/slowcoders/jdk-rtgc/test/lib \
        -J-Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d \
        -J-Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/test/lib \
        -J-Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/test/lib \
        -J-Dtest.modules=jdk.compiler \
        --add-modules jdk.compiler \
        -d /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d \
        -sourcepath /Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses:/Users/zeedh/slowcoders/jdk-rtgc/test/lib \
        -classpath /Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/test/lib /Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses/GCHiddenClass.java
        



pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
        -Dtest.vm.opts='-XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/tmp' \
        -Dtest.tool.vm.opts='-J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/tmp' \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dtest.timeout.factor=4.0 \
        -Dtest.nativepath=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg \
        -Dtest.name=runtime/HiddenClasses/GCHiddenClass.java \
        -Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses/GCHiddenClass.java \
        -Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses \
        -Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses:/Users/zeedh/slowcoders/jdk-rtgc/test/lib \
        -Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d \
        -Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/test/lib \
        -Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/test/lib \
        -Dtest.modules=jdk.compiler \
        -classpath /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/runtime/HiddenClasses/GCHiddenClass.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/runtime/HiddenClasses:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_runtime_HiddenClasses_GCHiddenClass_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar \
        GCHiddenClass