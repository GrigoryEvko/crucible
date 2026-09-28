// iota_v builds a vector with the index of each lane.  A scalar has no
// lanes and no value_type, so the vector type in the body refuses it.

#include <foundation/Simd.h>

int main() { return ::foundation::simd::iota_v<int>(); }
