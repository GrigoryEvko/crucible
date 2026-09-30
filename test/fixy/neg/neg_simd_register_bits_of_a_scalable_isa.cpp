// SVE is scalable: the register width belongs to the processor, not to
// the ISA.  register_bits therefore has no value for it, and a call for
// one is not a constant expression.

#include <fixy/atoms/Simd.h>

int main() { return ::fixy::atom::simd::register_bits(::fixy::atom::simd::SimdIsa::Sve); }
