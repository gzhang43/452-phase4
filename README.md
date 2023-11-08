# CSC 452 Phase 4a Request for Points Back
For testcase 20, our output shows that our program sometimes has the order of a read versus write finishing swapped, but
this should still be correct because our terminal outputs are correct and our reads and writes are occurring in parallel.
This may be occurring because the reads and writes are racing and can vary in when they finish.
