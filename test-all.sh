sh find_err.sh | xargs rm
make images CONF=macosx
ulimit -c unlimited; 
time make run-test-tier1 CONF=macosx debug
# make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/langtools:tier1
# make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/jdk:tier1
# make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/hotspot/jtreg:tier1
