# ./build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
#   -Xms12g -Xmx12g -jar ./renaissance/renaissance-gpl-0.14.1.jar -r 700 all

# --- With JSA
# ./build/macosx-x86_64-client-release/images/jdk/bin/java \
#   -Xms6g -Xmx6g -Xshare:on -jar ./renaissance/renaissance-gpl-0.14.1.jar scala-doku

TEST=dotty
# TEST=scala-doku
# TEST=-Xlog:gc=trace als
# -csv rtgc_result_${TEST}.csv

# docker run -it --rm -v `pwd`/renaissance:/renaissance azul/prime:17 java \
#   -Xms6g -Xmx6g \
#   -jar ./renaissance/renaissance-gpl-0.14.1.jar --no-forced-gc ${TEST}

# echo -------------------------------------------------
# echo Azul C4 done
# echo -------------------------------------------------

./build/macosx-x86_64-client-release/images/jdk/bin/java \
  -Xms6g -Xmx6g -Xlog:gc=trace \
  -jar ./renaissance/renaissance-gpl-0.14.1.jar ${TEST}

./build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
  -Xms6g -Xmx6g \
  -jar ./renaissance/renaissance-gpl-0.14.1.jar ${TEST}


echo -------------------------------------------------
echo RTGC done
echo -------------------------------------------------

