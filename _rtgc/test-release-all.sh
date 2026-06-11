sh find_err.sh | xargs rm
ulimit -c unlimited; 
make run-test-tier1 CONF=macosx-x86_64-client-release
# make test CONF=macosx-x86_64-client-release LOG_LEVEL=info TEST=jtreg:test/langtools:tier1
# make test CONF=macosx-x86_64-client-release LOG_LEVEL=info TEST=jtreg:test/jdk:tier1
# make test CONF=macosx-x86_64-client-release LOG_LEVEL=info TEST=jtreg:test/hotspot/jtreg:tier1
