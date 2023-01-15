# ./build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
#   -Xms12g -Xmx12g -jar ./renaissance/renaissance-gpl-0.14.1.jar -r 700 all

./build/macosx-x86_64-client-release/images/jdk/bin/java \
  -Xms6g -Xmx6g -Xshare:on -jar ./renaissance/renaissance-gpl-0.14.1.jar scala-doku

echo --------
echo

docker run -it --rm -v `pwd`/renaissance:/renaissance azul/prime:17 \
    java -Xms6g -Xmx6g -jar ./renaissance/renaissance-gpl-0.14.1.jar scala-doku