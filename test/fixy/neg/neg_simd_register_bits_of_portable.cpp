// Portable is the top of the SIMD order: one kernel for every instruction
// set, so it names no vector register at all.  register_bits has no value
// for it, and a call for one is not a constant expression.

#include <fixy/atoms/Simd.h>

int main() { return ::fixy::atom::simd::register_bits(::fixy::atom::simd::SimdIsa::Portable); }
