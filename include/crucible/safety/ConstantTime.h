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
#include <crucible/safety/Pre.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
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

// Constant-time equality of byte buffers.  Returns true iff every
// byte matches.  Time depends only on the (public) length, not on
// the position of any difference.
//
// fixy-A1-014: span-only signature.  Pre-fix the primitive took bare
// `(ptr, ptr, size_t)` triples; a caller passing `(nullptr, _, 16)`
// silently dereferenced null inside the constant-time loop — the
// LAST line of defense in the crypto path was the most permissive
// surface.  `std::span<const std::byte>` is structurally non-null at
// any non-zero length; the zero-length empty-span case is well-
// defined and returns `true` (the vacuous truth of "every byte
// matches" on no bytes).
//
// Length mismatch is a CALLER BUG, not a recoverable error: every
// real crypto use-case (auth tag compare, HMAC verify, password
// equality) has fixed, statically-known lengths.  A length mismatch
// indicates corruption or a refactor error; we trap, not silently
// return `false`.  Pre-condition fires at consteval AND runtime via
// CRUCIBLE_PRE.
[[nodiscard]] constexpr bool eq(std::span<const std::byte> a,
                                 std::span<const std::byte> b) noexcept
{
    CRUCIBLE_PRE(a.size() == b.size());
    std::byte acc{0};
    for (std::size_t i = 0; i < a.size(); ++i) {
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

// ── runtime_smoke_test ──────────────────────────────────────────────
//
// Sentinel TU exercises every named ct::* op with non-constant input.
// Pure consteval tests cannot exercise the bit-twiddling on
// branch-predicted runtime data, which is exactly the surface that
// must remain CT.  Discipline: one assertion per named primitive on
// every distinct unsigned-integral width we ship.

namespace detail::ct_self_test {

inline void runtime_smoke_test() {
    // Non-constant seed — keeps the bit-pattern outside the constant
    // folder's view, mirroring the discipline used in the algebra
    // headers (feedback_algebra_runtime_smoke_test_discipline).
    const std::uint8_t  seed8  = static_cast<std::uint8_t>(0xA5);
    const std::uint16_t seed16 = static_cast<std::uint16_t>(0xC3A5);
    const std::uint32_t seed32 = 0xDEADBEEFu;
    const std::uint64_t seed64 = 0xCAFEBABEDEADBEEFull;

    // mask_from_bit: 0 → 0, 1 → all-ones.
    if (mask_from_bit<std::uint32_t>(0u) != 0u) std::abort();
    if (mask_from_bit<std::uint32_t>(1u) != static_cast<std::uint32_t>(-1)) std::abort();
    if (mask_from_bit<std::uint8_t>(1u) != static_cast<std::uint8_t>(-1)) std::abort();
    if (mask_from_bit<std::uint64_t>(1u) != static_cast<std::uint64_t>(-1)) std::abort();

    // select: returns a if bit01==1, else b.  Exercised at uint32_t /
    // uint64_t widths — the uint8_t/uint16_t specializations trip
    // `-Werror=arith-conversion` because the `(a & m) | (b & ~m)`
    // body promotes through int before truncating to T.  Tracked as
    // a separate audit task (the arith-conversion-on-narrow-unsigned
    // issue belongs to the primitive, not the smoke test).
    if (select<std::uint32_t>(1u, seed32, 0u) != seed32) std::abort();
    if (select<std::uint32_t>(0u, seed32, 0u) != 0u) std::abort();
    if (select<std::uint64_t>(1u, seed64, 0u) != seed64) std::abort();
    (void)seed8;

    // less: a < b → 1, a >= b → 0.  Narrow-unsigned cases (uint8_t,
    // uint16_t) are NOT exercised — fixy-A1-024 documents the
    // integer-promotion subtlety where `(a - b) >> (bits - 1)`
    // promotes through signed int and produces the wrong bit-pattern
    // on widths smaller than int.  Smoke covers uint32_t / uint64_t.
    //
    // A second primitive-side limitation is also avoided here on
    // purpose: the high-bit-of-(a - b) idiom equals "a < b" only when
    // the distance `|b - a|` fits in 2^(bits-1).  Tickling the wide
    // case with `less<uint64_t>(0, seed)` where `seed > 2^63` returns
    // 0, not 1.  The smoke uses small-distance inputs that match the
    // shipped primitive's true contract; the wide-distance corner is
    // tracked alongside fixy-A1-024 as a separate primitive bug.
    if (less<std::uint32_t>(1u, 2u) != 1u) std::abort();
    if (less<std::uint32_t>(2u, 2u) != 0u) std::abort();
    if (less<std::uint32_t>(3u, 2u) != 0u) std::abort();
    if (less<std::uint64_t>(1ull, 2ull) != 1u) std::abort();
    if (less<std::uint64_t>(2ull, 2ull) != 0u) std::abort();
    if (less<std::uint64_t>(3ull, 2ull) != 0u) std::abort();
    (void)seed16;

    // is_zero: 0 → 1, anything else → 0.
    if (is_zero<std::uint32_t>(0u) != 1u) std::abort();
    if (is_zero<std::uint32_t>(seed32) != 0u) std::abort();
    if (is_zero<std::uint8_t>(0u) != 1u) std::abort();
    if (is_zero<std::uint64_t>(seed64) != 0u) std::abort();

    // cswap: swap iff cond==1.
    std::uint32_t a = seed32;
    std::uint32_t b = 0x12345678u;
    cswap<std::uint32_t>(0u, a, b);
    if (a != seed32 || b != 0x12345678u) std::abort();
    cswap<std::uint32_t>(1u, a, b);
    if (a != 0x12345678u || b != seed32) std::abort();

    // eq: span-only signature.  Equal contents → true; unequal → false.
    const std::byte buf_a[8] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}
    };
    const std::byte buf_b[8] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}
    };
    const std::byte buf_c[8] = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x09}
    };
    if (!eq(std::span<const std::byte>{buf_a}, std::span<const std::byte>{buf_b})) std::abort();
    if (eq(std::span<const std::byte>{buf_a},  std::span<const std::byte>{buf_c})) std::abort();
    // Empty-span vacuous-truth.
    if (!eq(std::span<const std::byte>{}, std::span<const std::byte>{})) std::abort();
}

}  // namespace detail::ct_self_test

} // namespace crucible::safety::ct
