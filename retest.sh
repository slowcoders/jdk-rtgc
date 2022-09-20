pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
        -XX:+UnlockExperimentalVMOptions \
        -Dtest.vm.opts='-XX:+UnlockExperimentalVMOptions -XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/tmp' \
        -Dtest.tool.vm.opts='-J-XX:+UnlockExperimentalVMOptions -J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/tmp' \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dtest.timeout.factor=4.0 \
        -Dtest.nativepath=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg \
        -Dtest.name=serviceability/dcmd/gc/RunFinalizationTest.java \
        -Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/serviceability/dcmd/gc/RunFinalizationTest.java \
        -Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/serviceability/dcmd/gc \
        -Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/serviceability/dcmd/gc:/Users/zeedh/slowcoders/jdk-rtgc/test/lib \
        -Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/classes/0/serviceability/dcmd/gc/RunFinalizationTest.d \
        -Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/classes/0/serviceability/dcmd/gc/RunFinalizationTest.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/classes/0/test/lib \
        -Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/classes/0/serviceability/dcmd/gc/RunFinalizationTest.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/serviceability/dcmd/gc:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/classes/0/test/lib \
        -Dtest.modules='java.base/jdk.internal.misc java.compiler java.management jdk.internal.jvmstat/sun.jvmstat.monitor' \
        -classpath /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/classes/0/serviceability/dcmd/gc/RunFinalizationTest.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/serviceability/dcmd/gc:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_serviceability_dcmd_gc_RunFinalizationTest_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar \
        RunFinalizationTest