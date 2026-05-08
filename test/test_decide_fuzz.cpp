// SPDX-License-Identifier: Apache-2.0
//
// test/test_decide_fuzz.cpp
//
// CONTRACT-090 + CONTRACT-091 — property-based fuzz harness for
// crucible::decide::* predicate procedures.
//
// CONSTRUCTION
// ------------
// For every fuzzable Decide procedure, this harness exercises the
// production fast path against a deliberately SLOW but TRANSPARENTLY
// CORRECT reference oracle (`crucible::decide::oracle::*`) on a
// large number of pseudo-random inputs derived from a deterministic
// Philox4x32-10 stream.  When fast and oracle disagree on any input,
// the test fails with a diagnostic that includes the rejected input
// — sufficient to reproduce the failure as a static_assert.
//
// The oracles live in `include/crucible/safety/DecideOracle.h` and
// each uses a DIFFERENT ALGORITHM than the production impl:
//
//   * `no_overflow_mul / no_overflow_sum` — widen to int128 / uint64
//     and bound, vs production `__builtin_*_overflow`.
//   * `all_in_range`                       — manual loop with explicit
//     `xs.empty()` short-circuit on the lo > hi guard, vs production
//     std::all_of.
//   * `strictly_increasing / weakly_increasing` — O(n²) all-pairs,
//     vs production single-pass adjacent compare.
//   * `is_power_of_two_le`                 — std::popcount + bound, vs
//     production `(x & (x-1)) == 0`-style trick.
//   * `factorization_eq`                   — widen-product fold, vs
//     production overflow-checked accumulator.
//   * `coprime`                            — textbook Euclidean GCD,
//     vs production binary / Stein's GCD reduction.
//   * `conjunction / disjunction`          — count-false / count-true,
//     vs production short-circuit any-false / any-true.
//   * `aligned_in_range`                   — sequential 4-clause check,
//     vs production fused short-circuit chain.
//
// "Different algorithm" is the key — an oracle that rebuilds the
// production body verbatim is tautological.  Every oracle in
// DecideOracle.h is derivable from the predicate definition by
// inspection alone.
//
// DETERMINISM
// -----------
// Fixed Philox seed.  Same input pool on every run, every machine,
// every architecture (Philox4x32-10 is bit-stable across platforms by
// construction).  This keeps the test in line with Crucible's DetSafe
// axiom: the test itself replays bit-identically.
//
// Each procedure uses a DISTINCT 64-bit Philox key (derived from a
// per-procedure constant).  A failure in one procedure cannot
// contaminate another's stream — the failing key + counter pair
// uniquely identifies the regression.
//
// COVERAGE STRENGTH
// -----------------
// 10,000 inputs per procedure.  Sufficient to flush short-circuit
// boundary bugs (off-by-one in length, wrong operator on the boundary
// element, missed signed/unsigned promotion in widen, wrong sign of
// the GCD-zero termination check, etc.) without being so slow it
// degrades the per-PR test gate.  For all_in_range / increasing /
// factorization_eq / conjunction / disjunction (span-based), each
// iteration draws a random length in [0, 16] so the harness covers
// the empty-span and unit-span vacuous-identity cases as well as
// non-trivial spans.
//
// On a typical x86-64 dev box this entire test runs in < 200 ms.
//
// DIAGNOSTIC ON FAILURE
// ---------------------
// Each procedure's fuzz block prints to stderr:
//   * Procedure name
//   * Iteration index in the Philox stream
//   * Inputs in a form that can be pasted into a static_assert
//   * Fast path's answer (true/false)
//   * Oracle's answer (true/false)
//
// This is sufficient to reproduce the failure as a static_assert
// witness in the appropriate test_decide.cpp section, then debug.
//
// WHY PROPERTY FUZZ IS LOAD-BEARING
// ---------------------------------
// Decide procedures are the LOAD-BEARING soundness gate of the
// CRUCIBLE_PRE infrastructure.  Every production call site that
// contracts on `dc::no_overflow_mul(a, b)` or `dc::aligned_in_range(
// off, lo, hi, aln)` is trusting that the procedure is BIT-IDENTICAL
// to its predicate definition.  A subtle bug — say, a `<=` where
// `<` is intended, or a missed sign extension on the widen — would
// silently admit unsafe inputs at the gate while the contract READS
// like it rejects them.
//
// The static_assert test_decide.cpp witnesses pin a small handful
// of named cases.  This harness pins the entire input space, modulo
// statistical cover.

#include <crucible/Philox.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/DecideOracle.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>

namespace {

namespace dc = crucible::decide;
namespace dco = crucible::decide::oracle;

// ── Tunables ───────────────────────────────────────────────────────
//
// 10K iterations per procedure × 11 procedures = 110K total.  Each
// iteration is a few hundred nanoseconds on a modern dev box; total
// fuzz cost well under 200 ms.  Increasing this is fine; decreasing
// it weakens the statistical claim.
constexpr int kIterations = 10'000;

// Maximum span length for span-based predicates.  Small enough that
// the O(n²) oracles (strictly_increasing, weakly_increasing) stay
// fast; large enough that the harness flushes off-by-one bugs in
// loops.  At length=16 the all-pairs oracle does 120 comparisons
// per iteration — under 1 µs each.
constexpr std::size_t kMaxSpanLen = 16;

// ── Per-procedure Philox keys ──────────────────────────────────────
//
// Each procedure gets a distinct 64-bit key.  The keys are arbitrary
// constants — what matters is that they DIFFER, so the streams are
// statistically independent.  A failure pins to ONE procedure; the
// failing (counter, key) tuple reproduces the input deterministically.

constexpr std::uint64_t kKeyMul          = 0xC001'C0DE'0001'0001ULL;
constexpr std::uint64_t kKeySum          = 0xC001'C0DE'0002'0002ULL;
constexpr std::uint64_t kKeyAllInRange   = 0xC001'C0DE'0003'0003ULL;
constexpr std::uint64_t kKeyStrictInc    = 0xC001'C0DE'0004'0004ULL;
constexpr std::uint64_t kKeyWeakInc      = 0xC001'C0DE'0005'0005ULL;
constexpr std::uint64_t kKeyPow2Le       = 0xC001'C0DE'0006'0006ULL;
constexpr std::uint64_t kKeyFactorEq     = 0xC001'C0DE'0007'0007ULL;
constexpr std::uint64_t kKeyCoprime      = 0xC001'C0DE'0008'0008ULL;
constexpr std::uint64_t kKeyConjunction  = 0xC001'C0DE'0009'0009ULL;
constexpr std::uint64_t kKeyDisjunction  = 0xC001'C0DE'000A'000AULL;
constexpr std::uint64_t kKeyAlignInRange = 0xC001'C0DE'000B'000BULL;

// ── Volatile sink to defeat dead-code elimination ──────────────────
//
// The fuzz body computes booleans then compares fast vs oracle; the
// optimizer would happily DCE the entire loop if it saw both calls
// as pure functions of pure inputs, since the result feeds nothing.
// Volatile sink forces the side-effect.

volatile int g_sink = 0;

// ── Diagnostic helpers ─────────────────────────────────────────────

[[noreturn]] void fail(const char* proc, int iter,
                       const char* msg) {
    std::fprintf(stderr,
        "test_decide_fuzz: MISMATCH in %s at iteration %d: %s\n",
        proc, iter, msg);
    std::exit(1);
}

// ── no_overflow_mul fuzz ───────────────────────────────────────────
//
// Sweep across uint8_t / uint16_t / uint32_t / uint64_t / int8_t /
// int16_t / int32_t / int64_t.  Each iteration draws 4×uint32 from
// Philox, takes the first two as candidate operands, and tests every
// (T, signedness) pair.  Sign promotion on T = int8_t / int16_t is
// what we're stressing — production uses __builtin_mul_overflow which
// is type-strict, but a buggy hand-rolled impl might widen one side.

template <typename T>
bool fuzz_pair_mul(std::uint32_t a32, std::uint32_t b32) {
    auto const a = static_cast<T>(a32);
    auto const b = static_cast<T>(b32);
    bool const fast = dc::no_overflow_mul<T>(a, b);
    bool const orcl = dco::no_overflow_mul_oracle<T>(a, b);
    if (fast != orcl) {
        std::fprintf(stderr,
            "  T=%s a=%lld b=%lld fast=%d oracle=%d\n",
            std::is_signed_v<T> ? "signed" : "unsigned",
            static_cast<long long>(a),
            static_cast<long long>(b),
            fast, orcl);
        return false;
    }
    return true;
}

void fuzz_no_overflow_mul() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyMul);
        if (!fuzz_pair_mul<std::uint8_t> (ctr[0], ctr[1])) fail("no_overflow_mul<u8>",  i, "see preceding line");
        if (!fuzz_pair_mul<std::uint16_t>(ctr[0], ctr[1])) fail("no_overflow_mul<u16>", i, "see preceding line");
        if (!fuzz_pair_mul<std::uint32_t>(ctr[0], ctr[1])) fail("no_overflow_mul<u32>", i, "see preceding line");
        if (!fuzz_pair_mul<std::uint64_t>(ctr[0], ctr[1])) fail("no_overflow_mul<u64>", i, "see preceding line");
        if (!fuzz_pair_mul<std::int8_t>  (ctr[0], ctr[1])) fail("no_overflow_mul<i8>",  i, "see preceding line");
        if (!fuzz_pair_mul<std::int16_t> (ctr[0], ctr[1])) fail("no_overflow_mul<i16>", i, "see preceding line");
        if (!fuzz_pair_mul<std::int32_t> (ctr[0], ctr[1])) fail("no_overflow_mul<i32>", i, "see preceding line");
        if (!fuzz_pair_mul<std::int64_t> (ctr[0], ctr[1])) fail("no_overflow_mul<i64>", i, "see preceding line");
        g_sink ^= static_cast<int>(ctr[0] ^ ctr[1]);
    }
}

// ── no_overflow_sum fuzz ───────────────────────────────────────────

template <typename T>
bool fuzz_pair_sum(std::uint32_t a32, std::uint32_t b32) {
    auto const a = static_cast<T>(a32);
    auto const b = static_cast<T>(b32);
    bool const fast = dc::no_overflow_sum<T>(a, b);
    bool const orcl = dco::no_overflow_sum_oracle<T>(a, b);
    if (fast != orcl) {
        std::fprintf(stderr,
            "  T=%s a=%lld b=%lld fast=%d oracle=%d\n",
            std::is_signed_v<T> ? "signed" : "unsigned",
            static_cast<long long>(a),
            static_cast<long long>(b),
            fast, orcl);
        return false;
    }
    return true;
}

void fuzz_no_overflow_sum() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeySum);
        if (!fuzz_pair_sum<std::uint8_t> (ctr[0], ctr[1])) fail("no_overflow_sum<u8>",  i, "see preceding line");
        if (!fuzz_pair_sum<std::uint16_t>(ctr[0], ctr[1])) fail("no_overflow_sum<u16>", i, "see preceding line");
        if (!fuzz_pair_sum<std::uint32_t>(ctr[0], ctr[1])) fail("no_overflow_sum<u32>", i, "see preceding line");
        if (!fuzz_pair_sum<std::uint64_t>(ctr[0], ctr[1])) fail("no_overflow_sum<u64>", i, "see preceding line");
        if (!fuzz_pair_sum<std::int8_t>  (ctr[0], ctr[1])) fail("no_overflow_sum<i8>",  i, "see preceding line");
        if (!fuzz_pair_sum<std::int16_t> (ctr[0], ctr[1])) fail("no_overflow_sum<i16>", i, "see preceding line");
        if (!fuzz_pair_sum<std::int32_t> (ctr[0], ctr[1])) fail("no_overflow_sum<i32>", i, "see preceding line");
        if (!fuzz_pair_sum<std::int64_t> (ctr[0], ctr[1])) fail("no_overflow_sum<i64>", i, "see preceding line");
        g_sink ^= static_cast<int>(ctr[2] ^ ctr[3]);
    }
}

// ── all_in_range fuzz ──────────────────────────────────────────────
//
// Generates a length in [0, kMaxSpanLen], fills with random uint16 in
// [0, 4096), picks lo/hi from a tight range so the predicate hits
// both true and false outcomes meaningfully.

void fuzz_all_in_range() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyAllInRange);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        std::uint16_t buf[kMaxSpanLen]{};
        for (std::size_t k = 0; k < len; ++k) {
            // Reuse Philox stream by re-seeding per element — keeps
            // determinism without consuming O(len) extra ctr draws.
            auto const sub = crucible::Philox::generate(
                static_cast<std::uint64_t>(i) * 64 + k,
                kKeyAllInRange);
            buf[k] = static_cast<std::uint16_t>(sub[0] & 0x0FFFu);
        }
        std::uint16_t const lo = static_cast<std::uint16_t>(ctr[1] & 0x0FFFu);
        std::uint16_t const hi = static_cast<std::uint16_t>(ctr[2] & 0x0FFFu);
        std::span<const std::uint16_t> xs{buf, len};
        bool const fast = dc::all_in_range<std::uint16_t>(xs, lo, hi);
        bool const orcl = dco::all_in_range_oracle<std::uint16_t>(xs, lo, hi);
        if (fast != orcl) {
            std::fprintf(stderr,
                "  len=%zu lo=%u hi=%u fast=%d oracle=%d\n",
                len, unsigned{lo}, unsigned{hi}, fast, orcl);
            fail("all_in_range", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[0]);
    }
}

// ── strictly_increasing fuzz ───────────────────────────────────────

void fuzz_strictly_increasing() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyStrictInc);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        std::int16_t buf[kMaxSpanLen]{};
        // Half the time generate a deliberately monotone sequence
        // (small positive deltas) so the predicate often answers
        // true; half the time generate noise so it often answers
        // false.  Forces both branches.
        bool const make_monotone = (ctr[0] & 1u) == 1u;
        std::int16_t prev = -8'000;
        for (std::size_t k = 0; k < len; ++k) {
            auto const sub = crucible::Philox::generate(
                static_cast<std::uint64_t>(i) * 64 + k,
                kKeyStrictInc);
            if (make_monotone) {
                std::int16_t const delta = static_cast<std::int16_t>(sub[0] & 0x07u);
                prev = static_cast<std::int16_t>(prev + delta);
                buf[k] = prev;
            } else {
                buf[k] = static_cast<std::int16_t>(sub[0]);
            }
        }
        std::span<const std::int16_t> xs{buf, len};
        bool const fast = dc::strictly_increasing<std::int16_t>(xs);
        bool const orcl = dco::strictly_increasing_oracle<std::int16_t>(xs);
        if (fast != orcl) {
            std::fprintf(stderr,
                "  len=%zu monotone=%d fast=%d oracle=%d\n",
                len, make_monotone ? 1 : 0, fast, orcl);
            fail("strictly_increasing", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[1]);
    }
}

// ── weakly_increasing fuzz ─────────────────────────────────────────

void fuzz_weakly_increasing() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyWeakInc);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        std::int16_t buf[kMaxSpanLen]{};
        bool const make_monotone = (ctr[0] & 1u) == 1u;
        std::int16_t prev = -8'000;
        for (std::size_t k = 0; k < len; ++k) {
            auto const sub = crucible::Philox::generate(
                static_cast<std::uint64_t>(i) * 64 + k,
                kKeyWeakInc);
            if (make_monotone) {
                // Weakly-increasing target: include occasional
                // duplicates (delta == 0) so the predicate's
                // <=-vs-< boundary is exercised.
                std::int16_t const delta = static_cast<std::int16_t>(sub[0] & 0x03u);
                prev = static_cast<std::int16_t>(prev + delta);
                buf[k] = prev;
            } else {
                buf[k] = static_cast<std::int16_t>(sub[0]);
            }
        }
        std::span<const std::int16_t> xs{buf, len};
        bool const fast = dc::weakly_increasing<std::int16_t>(xs);
        bool const orcl = dco::weakly_increasing_oracle<std::int16_t>(xs);
        if (fast != orcl) {
            std::fprintf(stderr,
                "  len=%zu monotone=%d fast=%d oracle=%d\n",
                len, make_monotone ? 1 : 0, fast, orcl);
            fail("weakly_increasing", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[2]);
    }
}

// ── is_power_of_two_le fuzz ────────────────────────────────────────

void fuzz_is_power_of_two_le() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyPow2Le);
        // uint32_t variant: sweep both x and bound across the full
        // 32-bit space; bias x toward power-of-2 forms half the time
        // by zeroing out all but one set bit.
        {
            std::uint32_t x = ctr[0];
            if ((ctr[2] & 1u) == 1u && x != 0u) {
                int const which = static_cast<int>(ctr[3] % 32);
                x = std::uint32_t{1} << which;
            }
            std::uint32_t const bound = ctr[1];
            bool const fast = dc::is_power_of_two_le<std::uint32_t>(x, bound);
            bool const orcl = dco::is_power_of_two_le_oracle<std::uint32_t>(x, bound);
            if (fast != orcl) {
                std::fprintf(stderr,
                    "  T=u32 x=%u bound=%u fast=%d oracle=%d\n",
                    unsigned{x}, unsigned{bound}, fast, orcl);
                fail("is_power_of_two_le<u32>", i, "see preceding line");
            }
        }
        // int32_t variant: same shape, signed.  Hits the x <= 0
        // early-reject branch in both fast and oracle.
        {
            std::int32_t x = static_cast<std::int32_t>(ctr[0]);
            if ((ctr[2] & 2u) == 2u && x > 0) {
                int const which = static_cast<int>(ctr[3] % 31);
                x = std::int32_t{1} << which;
            }
            std::int32_t const bound = static_cast<std::int32_t>(ctr[1]);
            bool const fast = dc::is_power_of_two_le<std::int32_t>(x, bound);
            bool const orcl = dco::is_power_of_two_le_oracle<std::int32_t>(x, bound);
            if (fast != orcl) {
                std::fprintf(stderr,
                    "  T=i32 x=%d bound=%d fast=%d oracle=%d\n",
                    int{x}, int{bound}, fast, orcl);
                fail("is_power_of_two_le<i32>", i, "see preceding line");
            }
        }
        g_sink ^= static_cast<int>(ctr[0] ^ ctr[3]);
    }
}

// ── factorization_eq fuzz ──────────────────────────────────────────
//
// Production semantic on the empty span is ambiguous (see
// DecideOracle.h header).  The harness skips length=0 on this
// procedure to elide the ambiguity; static_assert witnesses in
// test_decide.cpp pin the empty-span behavior directly.

void fuzz_factorization_eq() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyFactorEq);
        std::size_t const len = 1u + (ctr[0] % kMaxSpanLen);
        std::uint32_t buf[kMaxSpanLen]{};
        // Use small factors so the widened-product path is non-
        // trivial without overflowing in trivial cases.  A factor in
        // [1, 32) keeps a 16-element product at most 32^16 ≈ 1.2e24
        // — exercises the > UINT32_MAX rejection branch reliably.
        std::uint32_t fast_product = 1;
        bool fast_overflowed = false;
        for (std::size_t k = 0; k < len; ++k) {
            auto const sub = crucible::Philox::generate(
                static_cast<std::uint64_t>(i) * 64 + k,
                kKeyFactorEq);
            std::uint32_t const f = (sub[0] % 32u) + 1u;
            buf[k] = f;
            // Track an "honest" expected product in 64-bit, so we can
            // pick a `total` that's correct half the time and wrong
            // half the time.  This drives the equality branch.
            std::uint64_t const honest = static_cast<std::uint64_t>(fast_product) * f;
            if (honest > std::numeric_limits<std::uint32_t>::max()) {
                fast_overflowed = true;
            }
            fast_product = static_cast<std::uint32_t>(
                honest & 0xFFFF'FFFFu);
        }
        // Half the iterations: pass the correct product (when no
        // overflow), so the predicate must answer true.  Other half:
        // pass a deliberately wrong total.
        std::uint32_t total = fast_product;
        if ((ctr[1] & 1u) == 1u) {
            total ^= ctr[2] | 1u;  // perturb to wrong (non-zero xor)
        }
        // If the honest product overflowed uint32, force `total`
        // to something wrong as well (the predicate must reject).
        if (fast_overflowed) {
            total = ctr[2];  // arbitrary wrong value
        }
        std::span<const std::uint32_t> factors{buf, len};
        bool const fast = dc::factorization_eq<std::uint32_t>(factors, total);
        bool const orcl = dco::factorization_eq_oracle<std::uint32_t>(factors, total);
        if (fast != orcl) {
            std::fprintf(stderr,
                "  len=%zu total=%u overflowed=%d fast=%d oracle=%d\n",
                len, unsigned{total},
                fast_overflowed ? 1 : 0, fast, orcl);
            fail("factorization_eq", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(total);
    }
}

// ── coprime fuzz ───────────────────────────────────────────────────
//
// Sweep across signed and unsigned variants.  Bias half the inputs
// toward sharing a small common factor (multiply both by the same
// random ∈ [1, 8)) so the predicate hits both true and false reliably.

template <typename T>
bool fuzz_pair_coprime(std::uint32_t a32, std::uint32_t b32, std::uint32_t bias) {
    auto a = static_cast<T>(a32);
    auto b = static_cast<T>(b32);
    if ((bias & 1u) == 1u) {
        std::uint32_t const f = (bias >> 1) % 7u + 2u;  // [2, 8)
        a = static_cast<T>(static_cast<std::uint64_t>(a) * f);
        b = static_cast<T>(static_cast<std::uint64_t>(b) * f);
    }
    bool const fast = dc::coprime<T>(a, b);
    bool const orcl = dco::coprime_oracle<T>(a, b);
    if (fast != orcl) {
        std::fprintf(stderr,
            "  T=%s a=%lld b=%lld fast=%d oracle=%d\n",
            std::is_signed_v<T> ? "signed" : "unsigned",
            static_cast<long long>(a),
            static_cast<long long>(b),
            fast, orcl);
        return false;
    }
    return true;
}

void fuzz_coprime() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyCoprime);
        if (!fuzz_pair_coprime<std::uint32_t>(ctr[0], ctr[1], ctr[2])) fail("coprime<u32>", i, "see preceding line");
        if (!fuzz_pair_coprime<std::uint64_t>(ctr[0], ctr[1], ctr[2])) fail("coprime<u64>", i, "see preceding line");
        if (!fuzz_pair_coprime<std::int32_t> (ctr[0], ctr[1], ctr[2])) fail("coprime<i32>", i, "see preceding line");
        if (!fuzz_pair_coprime<std::int64_t> (ctr[0], ctr[1], ctr[2])) fail("coprime<i64>", i, "see preceding line");
        g_sink ^= static_cast<int>(ctr[3]);
    }
}

// ── conjunction fuzz ───────────────────────────────────────────────

void fuzz_conjunction() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyConjunction);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        bool buf[kMaxSpanLen]{};
        for (std::size_t k = 0; k < len; ++k) {
            // Bias toward all-true (75%) so the all-true accept path
            // is exercised regularly; a uniform 50/50 would mean the
            // accept path triggers only at p=0.5^len.
            auto const sub = crucible::Philox::generate(
                static_cast<std::uint64_t>(i) * 64 + k,
                kKeyConjunction);
            buf[k] = (sub[0] & 0x3u) != 0u;
        }
        std::span<const bool> xs{buf, len};
        bool const fast = dc::conjunction(xs);
        bool const orcl = dco::conjunction_oracle(xs);
        if (fast != orcl) {
            std::fprintf(stderr,
                "  len=%zu fast=%d oracle=%d\n", len, fast, orcl);
            fail("conjunction", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[1]);
    }
}

// ── disjunction fuzz ───────────────────────────────────────────────

void fuzz_disjunction() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyDisjunction);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        bool buf[kMaxSpanLen]{};
        for (std::size_t k = 0; k < len; ++k) {
            // Bias toward all-false (75%) so the all-false reject
            // path is exercised regularly (mirror image of conjunction).
            auto const sub = crucible::Philox::generate(
                static_cast<std::uint64_t>(i) * 64 + k,
                kKeyDisjunction);
            buf[k] = (sub[0] & 0x3u) == 0u;
        }
        std::span<const bool> xs{buf, len};
        bool const fast = dc::disjunction(xs);
        bool const orcl = dco::disjunction_oracle(xs);
        if (fast != orcl) {
            std::fprintf(stderr,
                "  len=%zu fast=%d oracle=%d\n", len, fast, orcl);
            fail("disjunction", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[1]);
    }
}

// ── aligned_in_range fuzz ──────────────────────────────────────────
//
// 4-clause predicate.  Bias inputs so each clause has a meaningful
// probability of being the rejecting one:
//   * alignment: 50% chance of zero (zero-guard branch); when nonzero,
//     pick from {1, 2, 4, 8, 16, 32, 64, 128} so % is well-defined.
//   * value: 50% chance of being aligned (multiple of alignment);
//     50% of being a free uint64.
//   * lo, hi: drawn from a tighter range so out-of-range and
//     in-range outcomes are both common.

void fuzz_aligned_in_range() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(
            static_cast<std::uint64_t>(i), kKeyAlignInRange);
        std::uint64_t alignment;
        if ((ctr[0] & 1u) == 1u) {
            alignment = 0;
        } else {
            int const which = static_cast<int>(ctr[0] >> 1) % 8;
            alignment = std::uint64_t{1} << which;  // 1, 2, 4, ..., 128
        }
        std::uint64_t value;
        if ((ctr[1] & 1u) == 1u && alignment != 0u) {
            // Force aligned: pick a multiple of alignment in a wide range.
            value = (static_cast<std::uint64_t>(ctr[2]) % 1024u) * alignment;
        } else {
            value = ctr[2] & 0xFFFFu;  // small free value
        }
        std::uint64_t const lo = (ctr[3] >> 16) & 0xFFu;        // [0, 256)
        std::uint64_t const hi = lo + (ctr[3] & 0x0FFFu);       // [lo, lo + 4096)
        bool const fast = dc::aligned_in_range(value, lo, hi, alignment);
        bool const orcl = dco::aligned_in_range_oracle(value, lo, hi, alignment);
        if (fast != orcl) {
            std::fprintf(stderr,
                "  value=%llu low=%llu high=%llu align=%llu fast=%d oracle=%d\n",
                static_cast<unsigned long long>(value),
                static_cast<unsigned long long>(lo),
                static_cast<unsigned long long>(hi),
                static_cast<unsigned long long>(alignment),
                fast, orcl);
            fail("aligned_in_range", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(value);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr,
        "test_decide_fuzz: %d iterations × 11 procedures "
        "(Philox4x32-10 stream, fixed seed)\n",
        kIterations);

    fuzz_no_overflow_mul();
    fuzz_no_overflow_sum();
    fuzz_all_in_range();
    fuzz_strictly_increasing();
    fuzz_weakly_increasing();
    fuzz_is_power_of_two_le();
    fuzz_factorization_eq();
    fuzz_coprime();
    fuzz_conjunction();
    fuzz_disjunction();
    fuzz_aligned_in_range();

    if (g_sink == 0) {
        // Every per-procedure fuzz xors something nontrivial into
        // the sink; if all of them xored to exactly 0 the harness
        // is suspect (likely DCE'd or skipped).
        std::fprintf(stderr,
            "test_decide_fuzz: WARNING — sink is 0; harness may have been DCE'd\n");
        return 1;
    }
    std::fprintf(stderr,
        "test_decide_fuzz: all %d × 11 = %d iterations passed\n",
        kIterations, kIterations * 11);
    return 0;
}
