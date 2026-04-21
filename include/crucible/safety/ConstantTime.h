#pragma once

// ── crucible::safety::ct ────────────────────────────────────────────
//
// Constant-time primitives for crypto paths and any code path
// handling Secret<T> values where the execution trace must not
// depend on secret content.
//
//   Axiom coverage: DetSafe (side-channel resistance).
//   Runtime cost:   equivalent to branched code; sometimes faster
//                   (no branch-miss penalty on predictable-pattern hot
//                   paths).
//
// Discipline:
//   - In a `with Crypto` context, NO branches on Secret<T> values.
//   - NO secret-dependent array indices.
//   - NO variable-latency operations (division by secret, modular
//     reduction with secret modulus, etc.).
//
// These primitives do not and cannot detect all CT violations at
// compile time — it is a discipline reinforced by review.  `ct::`
// primitives are building blocks; the crypto-path author composes
// them.
//
// Reference: the constant-time coding handbook.  All operations use
// bitwise ops + integer arithmetic, no branches, no lookups indexed
// by secret data.
//
// ── Scope: what ct::* IS and IS NOT for ─────────────────────────────
//
// IS for (when those subsystems land):
//   - Canopy peer authentication:  HMAC tag comparison (ct::eq),
//     session-token equality, challenge-response verification.
//   - Cipher cold-tier encryption:  MAC verification on serialized
//     DAG/object bytes retrieved from S3/GCS.
//   - TLS/mTLS certificate pinning inside the Canopy mesh.
//   - Any future code path handling credentials, keys, or auth tags
//     where the execution trace must not leak secret content.
//
// IS NOT for:
//   - **Philox4x32 RNG** (crucible::Philox).  Philox is counter-based,
//     deterministic, and its reproducibility is the DetSafe contract —
//     the key + counter stream IS the replay proof.  Classifying key
//     material would fight DetSafe: the whole point of Philox is that
//     the same (counter, key) produces the same bits on any hardware,
//     and downstream code MUST be able to compare / log / serialize /
//     hash those bits.  Timing side-channel is not a threat; the PRNG
//     state is part of the public replay contract.
//   - Model weights, activations, gradients.  These are the artifact
//     being trained/served; reproducibility across restarts and mesh
//     reshards is the whole Cipher/Canopy promise.  Constant-time
//     access would break determinism and add no security value.
//   - Content hashes, schema hashes, merkle roots.  Public by design
//     (Merkle DAG audit trail, KernelCache key).  No secret content.
//   - Any comparison of SchemaHash / ContentHash / CallsiteHash /
//     OpIndex etc.  These are identity, not credentials.
//
// If a new caller wants ct::* on one of the "IS NOT" list values,
// rethink — they are public inputs, not secrets, and adding ct::*
// there introduces gratuitous runtime cost for zero security gain.
// If in doubt, grep for `Secret<` — only values wrapped in Secret<T>
// should flow through ct::*.  A call site without a Secret<T> in its
// backing data is almost certainly wrong-scoped.

#include <crucible/Platform.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::safety::ct {

// Convert a 0/1 bool-like value to an all-zeros or all-ones mask
// of the target integer type.  Input must already be 0 or 1 — this
// is the caller's invariant.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T mask_from_bit(T bit01) noexcept {
    // bit01 is 0 or 1; negating turns into 0 or all-ones (two's complement).
    return static_cast<T>(T{0} - (bit01 & T{1}));
}

// Select between a and b based on a 0/1 bit.
// Returns a when bit01 == 1, b when bit01 == 0.  Branch-free.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T select(T bit01, T a, T b) noexcept {
    const T m = mask_from_bit(bit01);
    return (a & m) | (b & ~m);
}

// Constant-time equality of raw byte buffers.  Returns true iff
// every byte matches.  Time depends only on length n, not on the
// position of any difference.
[[nodiscard]] constexpr bool eq(const std::byte* a,
                                 const std::byte* b,
                                 std::size_t n) noexcept
{
    std::byte acc{0};
    for (std::size_t i = 0; i < n; ++i) {
        acc |= a[i] ^ b[i];
    }
    return acc == std::byte{0};
}

// Constant-time less-than comparison on unsigned integers.  Returns
// 1 if a < b else 0.  Branch-free on standard hardware (the
// subtract/shift idiom compiles to straight-line code).
template <std::unsigned_integral T>
[[nodiscard]] constexpr T less(T a, T b) noexcept {
    // Borrow bit of (a - b) is set iff a < b.
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    return static_cast<T>((a - b) >> (bits - 1)) & T{1};
}

// Constant-time is-zero.  Returns 1 if x == 0 else 0.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T is_zero(T x) noexcept {
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    // (!x) ≡ (((x - 1) & ~x) >> (bits-1)) — set when x was 0.
    const T neg_x = static_cast<T>(T{0} - x);
    return static_cast<T>((neg_x | x) >> (bits - 1) ^ T{1});
}

// Conditional-swap: swap a and b if cond == 1, no-op if cond == 0.
// Branch-free.
template <std::unsigned_integral T>
constexpr void cswap(T cond01, T& a, T& b) noexcept {
    const T m = mask_from_bit(cond01);
    const T d = (a ^ b) & m;
    a ^= d;
    b ^= d;
}

} // namespace crucible::safety::ct
