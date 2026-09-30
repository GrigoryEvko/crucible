// SwissTable.h checks the stride of its probe against the register width
// of the active ISA, and fixy::atom::simd::register_bits gives that width.
// This file tries to give each ISA a width of its own: it writes an
// explicit specialization of register_bits.  The width is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <fixy/atoms/Simd.h>

#include <cstdint>

template <>
consteval std::uint16_t fixy::atom::simd::register_bits(fixy::atom::simd::SimdIsa) noexcept {
    return 512;
}

int main() { return 0; }
