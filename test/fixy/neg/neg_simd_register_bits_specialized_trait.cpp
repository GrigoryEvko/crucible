// SwissTable.h checks the stride of its probe against the register width
// of the active ISA.  This file tries to change that width for SSE4.2,
// which is the active ISA of a build for that tier.  It specializes the
// variable template that the width once was, because a translation unit
// that specializes a value that a check reads changes the check.  The
// width is a function that cannot be specialized, so the specialization
// has nothing to name.

#include <fixy/atoms/Simd.h>

#include <cstdint>

template <>
inline constexpr std::uint16_t fixy::atom::simd::register_bits_v<fixy::atom::simd::SimdIsa::Sse42> = 512;

int main() { return 0; }
