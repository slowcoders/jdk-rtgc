###
1. node-type 및 ref-count 정보는 lock 에 의해 evacuation 됨. GC 전 복구 필요.
2. acylic node 도 hashCode 를 저장하는 순간에 HashedAcyclicNode 생성 필요.