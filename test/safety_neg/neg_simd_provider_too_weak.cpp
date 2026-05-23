// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-256 HS14 fixture 1/2 — within-trunk admission (provider-too-weak).
//
// A consumer requiring an AVX512BW provider accepts any
// SimdWidthPinned<W, T> whose pinned ISA SUBSUMES Avx512Bw (Avx512Bw ⊑ W
// in the SimdIsaLattice partial order).  A SimdWidthPinned<Avx2, T> does
// NOT subsume Avx512Bw — within the x86 trunk Avx2 sits BELOW Avx512Bw,
// so an AVX2 provider lacks the AVX512 instructions the consumer needs.
// The gate MUST reject it; without the rejection a kernel dispatched to
// AVX2-only capability would execute AVX512 instructions and #UD.
//
// Distinct mismatch class from neg_simd_relax_up_or_cross_trunk.cpp: this
// rejects at the CONSUMER admission boundary (a `requires satisfies<R>`
// gate) along a COMPARABLE within-trunk pair, whereas the sister fixture
// rejects at a PRODUCER `relax<>` boundary on the INCOMPARABLE cross-trunk
// direction.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" naming the satisfies-gated require_avx512_provider template.

#include <crucible/safety/SimdWidthPinned.h>

namespace sf = ::crucible::safety;
using Si_t   = sf::SimdIsa_v;

// A consumer requiring at least an AVX512BW-capable provider.
template <typename W>
    requires (W::template satisfies<Si_t::Avx512Bw>)
[[nodiscard]] constexpr int require_avx512_provider(W const& w) noexcept {
    return w.peek();
}

int main() {
    sf::SimdWidthPinned<Si_t::Avx2, int> avx2_kernel{42};
    // Avx2 does NOT subsume Avx512Bw — admission MUST be rejected.
    return require_avx512_provider(avx2_kernel);
}
