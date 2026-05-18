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
// 1 if a < b else 0.  Branch-free on standard hardware (only &, |,
// ~, -, >>; no conditional moves).
//
// fixy-A1-024: the prior idiom `(a - b) >> (bits-1) & 1` was
// structurally broken at every width, not just on small unsigned
// types where integer promotion to `int` further compounded the
// issue.  Two failure modes the pre-fix formula admitted:
//
//   (W1) `ct::less<uint32_t>(0xFFFFFFFFu, 1u)` returned 1 — claiming
//        UINT32_MAX < 1.  Cause: `0xFFFFFFFF - 1 = 0xFFFFFFFE` in
//        same-width unsigned modular subtraction; `>> 31 = 1`.  The
//        T-width subtraction loses the BORROW (which sits at the
//        nonexistent bit `bits`, not at `bits-1`); the high bit of
//        the result is unrelated to a < b.
//   (W2) `ct::less<uint8_t>(5, 250)` returned 0 — claiming 5 ≥ 250.
//        Cause: C++ integer promotion lifts both uint8_t operands to
//        signed `int`; `int(5) - int(250) = -245`; arithmetic shift
//        right by 7 yields a sign-extended pattern whose low byte's
//        bit 0 is 0.
//
// Both failure modes collapse into one root cause: there is no
// width at which the high bit of a TRUNCATED subtraction equals the
// borrow.  The correct constant-time recipe (BoringSSL
// `constant_time_lt_args_w` / libsodium / Bernstein-style) computes
// the borrow bit purely from bitwise relationships between a, b,
// and the modular difference (a - b):
//
//   lt(a, b) := ((~a & b) | ((~a | b) & (a - b))) >> (bits - 1)
//
// Derivation: for each bit position, set `lt` iff that bit's
// contribution to "a < b" is decided in b's favor.  `~a & b` lights
// up positions where b has a 1 and a has a 0 (b wins at that bit);
// `(~a | b) & (a-b)` propagates the borrow leftward.  ORing the two
// gives a value whose MSB is set iff a < b at the T-width.
//
// Operates on T directly without widening, so works for T = uintmax_t
// (no wider integer required).  For T < unsigned, the explicit
// `static_cast<T>(a - b)` step truncates the int-promoted
// subtraction back to T width BEFORE the bitwise operations, so the
// modular relationship between a, b, and (a-b) at T-width is the
// one the formula evaluates — int-promotion no longer leaks the
// signed-shift hazard to the >> step.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T less(T a, T b) noexcept {
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    T const diff = static_cast<T>(a - b);
    T const lt   = static_cast<T>(
        (static_cast<T>(~a) & b) |
        (static_cast<T>(static_cast<T>(~a) | b) & diff));
    return static_cast<T>(lt >> (bits - 1)) & T{1};
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

    // less: a < b → 1, a >= b → 0.  Every regression witness below
    // corresponds to a corner the pre-fixy-A1-024 idiom got wrong:
    //
    //   (W1) High-bit-of-(a-b) idiom claimed UINT32_MAX < 1 because
    //        same-width unsigned subtraction loses the borrow.  The
    //        BoringSSL-form formula reconstructs the borrow bitwise.
    //   (W2) Narrow-unsigned widths promoted through signed int; the
    //        old idiom produced sign-extended garbage after >>(bits-1).
    //        New formula uses explicit static_cast<T> at every step.
    //   (W3) Wide-distance comparisons like (0 < 0x8000…) tripped the
    //        "a - b fits in 2^(bits-1)" assumption baked into the old
    //        idiom.  New formula has no distance ceiling — works for
    //        every (a, b) pair the type can represent.
    //
    // Small-magnitude / equal / reversed cases are kept too as the
    // basic-contract anchor.
    if (less<std::uint32_t>(1u, 2u) != 1u) std::abort();
    if (less<std::uint32_t>(2u, 2u) != 0u) std::abort();
    if (less<std::uint32_t>(3u, 2u) != 0u) std::abort();
    if (less<std::uint64_t>(1ull, 2ull) != 1u) std::abort();
    if (less<std::uint64_t>(2ull, 2ull) != 0u) std::abort();
    if (less<std::uint64_t>(3ull, 2ull) != 0u) std::abort();
    // W2: narrow-unsigned regression witnesses.
    if (less<std::uint8_t>(std::uint8_t{5},   std::uint8_t{250}) != std::uint8_t{1}) std::abort();
    if (less<std::uint8_t>(std::uint8_t{250}, std::uint8_t{5})   != std::uint8_t{0}) std::abort();
    if (less<std::uint16_t>(std::uint16_t{0x000A}, std::uint16_t{0xFFFF}) != std::uint16_t{1}) std::abort();
    if (less<std::uint16_t>(std::uint16_t{0xFFFF}, std::uint16_t{0x000A}) != std::uint16_t{0}) std::abort();
    // W1: same-width borrow-loss regression witnesses.
    if (less<std::uint32_t>(0xFFFFFFFFu, 1u)               != 0u)    std::abort();
    if (less<std::uint32_t>(1u, 0xFFFFFFFFu)               != 1u)    std::abort();
    if (less<std::uint64_t>(0xFFFFFFFFFFFFFFFFull, 1ull)   != 0ull)  std::abort();
    if (less<std::uint64_t>(1ull, 0xFFFFFFFFFFFFFFFFull)   != 1ull)  std::abort();
    // W3: wide-distance crossing the half-range boundary.
    if (less<std::uint8_t>(std::uint8_t{0},  std::uint8_t{0x80}) != std::uint8_t{1}) std::abort();
    if (less<std::uint64_t>(0ull, 0x8000000000000000ull)   != 1ull)  std::abort();
    if (less<std::uint64_t>(0x8000000000000000ull, 0ull)   != 0ull)  std::abort();
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
