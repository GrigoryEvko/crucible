#pragma once

// The one mixer of the tree: fmix64, a permutation of the 64-bit words, and
// combine_ids, the step of each fold.  The ids of foundation/reflect/Hash.h,
// the hash of the Protocol family, the canonical form of a net and the
// content hashes of the product fold through these two bodies.  This header
// includes no other header of the base, so each header can include it.

#include <cstdint>

namespace foundation::reflect {

// The one mixing primitive in the tree.  Accumulator folds do not call it
// directly — they call `combine_ids(state, input)` just below, which is this
// function behind one displacement step.  The displacement is load-bearing,
// and the last paragraph says why.
//
// This is the MurmurHash3 finalizer.  Each step is invertible modulo 2^64 —
// an xor-shift is its own inverse family, and a multiply by an odd constant
// is invertible — so fmix64 is a PERMUTATION of the 64-bit words.  That one
// property is why it is the only mixer here:
//
//   * A permutation has no absorbing value.  There is no `x` for which
//     `fmix64(state ^ x)` forgets `state`, so no single input can erase the
//     prefix of a fold.  Measured: zero collisions over 400,000 consecutive
//     inputs, as a bijection must give.
//   * It is total.  Every 64-bit input maps to a distinct 64-bit output, so
//     a content hash of 0 — the KernelCache empty-slot sentinel, and a value
//     make_region's own postcondition refuses — can only arise from the one
//     accumulator state that maps to it, never from a degenerate input.
//
// Do not replace it with a folded 128-bit product such as
// `lo(a*b) ^ hi(a*b)`.  That mix loses bits, and it has two absorbing
// values for its second operand: 0 gives 0 and ~0 gives ~0 for every
// first operand.  Real tensor metadata reaches both.  A uint8 tensor on
// CPU:0 packs to zero, and ScalarType::Undefined is int8_t(-1), which
// sign-extends to all ones.  Two different regions then get one content
// hash, and that hash keys the compiler cache.
//
// Two reasons a fold calls combine_ids rather than fmix64 on a bare xor.
//
// A single `fmix64(a ^ b)` is xor-symmetric, so it cannot on its own tell
// operand order.  Order sensitivity comes from the CHAIN: the accumulator
// has already been through fmix64 and the input has not, so the two are not
// interchangeable across steps.  Do not flatten a fold into one xor.
//
// And `fmix64(0)` is 0.  A permutation still has exactly one preimage of
// zero, and for the bare form that preimage is `input == accumulator`.
// Ordinary data reaches that case, because a seed and a schema hash can
// come from the same constant.  A region whose schema hash is
// `1 * 0x9E3779B97F4A7C15`, the seed of the fold, makes the first bare
// step `fmix64(0)`, and its content hash is then 0, the KernelCache
// empty-slot sentinel.  combine_ids displaces the input by the golden
// ratio and mixes the bits of the accumulator before the xor.  The zero
// preimage is then not `input == accumulator`.  Measured: each of 200,000
// self-mixes gives 0 through the bare form, and none gives 0 through
// combine_ids.
[[nodiscard]] constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// The accumulator step every fold in the tree spells.  Boost-style combine
// — a golden-ratio salt and two shifts of the accumulator — finalized by
// fmix64 above.  It is order-sensitive: combining a with b differs from
// combining b with a, and callers that fold a sequence rely on that.
//
// Read the fmix64 block above for why a fold calls this rather than
// fmix64 on a bare xor.
//
// constexpr and not consteval because one body has to serve both the
// compile-time fold and a runtime check that re-derives the same value.
// A second copy of this body under any other name is a drift surface:
// changing the salt, the mix or the finalizer would leave that copy stale
// and change the shared key while every assertion against it still passed.
// The rule home of the quarantine plugin refuses a
// second body (utils/tools/quarantine/home.cpp).
[[nodiscard]] constexpr std::uint64_t combine_ids(std::uint64_t a, std::uint64_t b) noexcept {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return fmix64(a);
}

}  // namespace foundation::reflect
