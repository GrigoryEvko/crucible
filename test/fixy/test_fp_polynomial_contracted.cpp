// test_fp_polynomial.cpp compiled a second time, with -ffp-contract=on and
// fused multiply-add instructions.  The registration in CMakeLists.txt
// sets the two flags and defines CRUCIBLE_TEST_FP_CONTRACTED, and the test
// checks that the build does contract a written a * b + c.

#include "test_fp_polynomial.cpp"
