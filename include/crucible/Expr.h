#pragma once

#include <cstdint>

namespace crucible {

namespace detail {

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
// It replaced `wymix(a, b) = lo(a*b) ^ hi(a*b)`, deleted 2026-09-15, which
// was none of those things.  The 128-bit product folded down to 64 bits is
// lossy by construction and had two absorbing values for its second operand:
// `wymix(a, 0)` is 0 and `wymix(a, ~0)` is ~0, for every `a`.  Both were
// reachable from real tensor metadata — a uint8 tensor on CPU:0 packs to
// zero, and ScalarType::Undefined is int8_t(-1), which sign-extends to all
// ones — so two structurally different regions hashed identically, on the
// value that keys the compiler's cache.  Three call sites had already
// patched around the zero case locally with three different spellings, each
// commenting that wymix collapses on it; the per-op tensor fold, the one
// that matters most, never got the treatment.
//
// wymix bought nothing for that.  Its own comment claimed it avalanched
// better than a shift-and-xor chain; measured against this tree's other
// fmix64-based combiner it avalanches identically, 31.93 against 32.04
// flipped output bits per input bit flipped, both at the ideal 32 of 64.
//
// Two reasons a fold calls combine_ids rather than fmix64 on a bare xor.
//
// A single `fmix64(a ^ b)` is xor-symmetric, so it cannot on its own tell
// operand order.  Order sensitivity comes from the CHAIN: the accumulator
// has already been through fmix64 and the input has not, so the two are not
// interchangeable across steps.  Do not flatten a fold into one xor.
//
// And `fmix64(0)` is 0.  A permutation still has exactly one preimage of
// zero, and for the bare form that preimage is `input == accumulator` — a
// coincidence that ordinary data reaches, because a seed and a schema hash
// can be built from the same constant.  It happened on the first run of
// this change: test_cipher_commit_lifetime mints a region whose schema hash
// is `1 * 0x9E3779B97F4A7C15`, which is the fold's own seed, so the first
// step produced `fmix64(0)` and the content hash came out 0 — the
// KernelCache empty-slot sentinel, caught by make_region's postcondition.
// combine_ids displaces the input by the golden ratio and mixes the
// accumulator's own bits before the xor, so the zero preimage stops
// coinciding with `input == accumulator`.  Measured: 200,000 of 200,000
// self-mixes give 0 through the bare form, 0 of 200,000 through combine_ids.
constexpr uint64_t fmix64(uint64_t k) {
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
//
// MerkleDag.h folds with it, and Expr.h is upstream of MerkleDag.h.
[[nodiscard]] constexpr uint64_t combine_ids(uint64_t a, uint64_t b) noexcept {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return fmix64(a);
}

}  // namespace detail

}  // namespace crucible
