// Portable is the top of the SIMD order: one kernel for every instruction
// set, so it names no vector register at all.  register_bits_v has no value
// for it, and naming one fails the fixed-width constraint.

#include <fixy/atoms/Simd.h>

int main() { return ::fixy::atom::simd::register_bits_v<::fixy::atom::simd::SimdIsa::Portable>; }
