// SVE is scalable: the register width belongs to the processor, not to
// the ISA.  register_bits_v therefore has no value for it, and naming one
// fails the constraint that the ISA has a fixed register width.

#include <fixy/atoms/Simd.h>

int main() { return ::fixy::atom::simd::register_bits_v<::fixy::atom::simd::SimdIsa::Sve>; }
