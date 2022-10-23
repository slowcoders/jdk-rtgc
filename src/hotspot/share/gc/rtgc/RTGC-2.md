1) Flags
2) RootRefCount
3) ShortcutId
4) AnchorList

1. shorcut_id
   0: Not Trackable
   1: Not Anchored
   2: Path Broken
   3: Safely Anchored
   4 ~: Shortcut-Id

isYoungRoot
dirtyReferrerPoints
hasMultiRef
tracking-state: 
   0: Not-Visited
   1: In-Tracking
   2: Garbage
   3: In-Circuit

1) R 을 _visitedList 에 추가한다.
2) R 의 Anchor 중 In_Tracing 객체가 있으면, 즉 순환 참조가 발견되면, 
   Circuit 에 속한 객체들을 In-Circuit marking 한다.
2) 객체 R의 Safe-Anchor 를 찾지 못하면, 
   - In-Circuit 상태가 아니면, R을 garbage Marking 한다.
3) 