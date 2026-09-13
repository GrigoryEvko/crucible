// SPDX-License-Identifier: Apache-2.0
//
// Every predicate here is checked against a reference oracle that
// reaches the same answer by a different route: widening into a larger
// integer rather than asking the compiler about overflow, comparing
// all pairs rather than each adjacent pair once, a textbook greatest
// common divisor rather than a binary one, counting rather than short
// circuiting.  The difference in algorithm is what makes the check
// worth anything.  An oracle that restated the production body would
// agree with it about every bug it has.
//
// The inputs come from a counter-based generator with a fixed seed, so
// the pool is identical on every run and every machine and the test
// replays bit for bit.  Each predicate draws on its own key, so a
// failure names one predicate and the failing counter reproduces its
// input exactly.
//
// Ten thousand inputs per predicate is enough to flush the boundary
// bugs these predicates are prone to: an off-by-one in a length, the
// wrong comparison on the boundary element, a missed promotion while
// widening, the wrong sign on a termination test.  The span-based
// predicates draw a fresh length each round, so the empty span and the
// single-element span get covered alongside the interesting ones.
//
// These predicates are what the contract macros rest on.  A call site
// that guards itself with one is trusting that the predicate means
// what its name says.  A comparison that admits one value too many
// would let unsafe inputs through a gate that still reads as though it
// closed.  The named compile-time witnesses pin a handful of cases;
// this harness pins the input space up to statistical cover.
//
// A mismatch prints the predicate, the iteration index, the inputs and
// both answers, which is enough to restate the case as a compile-time
// witness.

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

// Raising this is free.  Lowering it weakens the statistical claim.
constexpr int kIterations = 10'000;

// Short enough that the all-pairs oracles stay cheap, long enough to
// flush an off-by-one in a loop.
constexpr std::size_t kMaxSpanLen = 16;

// The keys themselves are arbitrary.  What matters is that they
// differ, so that the streams are independent and a failure points at
// one predicate.

constexpr std::uint64_t kKeyMul = 0xC001'C0DE'0001'0001ULL;
constexpr std::uint64_t kKeySum = 0xC001'C0DE'0002'0002ULL;
constexpr std::uint64_t kKeyAllInRange = 0xC001'C0DE'0003'0003ULL;
constexpr std::uint64_t kKeyStrictInc = 0xC001'C0DE'0004'0004ULL;
constexpr std::uint64_t kKeyWeakInc = 0xC001'C0DE'0005'0005ULL;
constexpr std::uint64_t kKeyPow2Le = 0xC001'C0DE'0006'0006ULL;
constexpr std::uint64_t kKeyFactorEq = 0xC001'C0DE'0007'0007ULL;
constexpr std::uint64_t kKeyCoprime = 0xC001'C0DE'0008'0008ULL;
constexpr std::uint64_t kKeyConjunction = 0xC001'C0DE'0009'0009ULL;
constexpr std::uint64_t kKeyDisjunction = 0xC001'C0DE'000A'000AULL;
constexpr std::uint64_t kKeyAlignInRange = 0xC001'C0DE'000B'000BULL;

// Both calls are pure functions of pure inputs and the comparison
// feeds nothing, so without a volatile sink the optimizer is entitled
// to delete the whole loop.

volatile int g_sink = 0;

[[noreturn]] void fail(const char* proc, int iter, const char* msg) {
    std::fprintf(stderr, "test_decide_fuzz: MISMATCH in %s at iteration %d: %s\n", proc, iter, msg);
    std::exit(1);
}

// The narrow signed widths are the point of the sweep.  A hand-rolled
// check that widened one operand and not the other would agree with
// the compiler's on the wide types and disagree here.

template <typename T>
bool fuzz_pair_mul(std::uint32_t a32, std::uint32_t b32) {
    auto const a = static_cast<T>(a32);
    auto const b = static_cast<T>(b32);
    bool const fast = dc::no_overflow_mul<T>(a, b);
    bool const orcl = dco::no_overflow_mul_oracle<T>(a, b);
    if (fast != orcl) {
        std::fprintf(stderr, "  T=%s a=%lld b=%lld fast=%d oracle=%d\n", std::is_signed_v<T> ? "signed" : "unsigned",
                     static_cast<long long>(a), static_cast<long long>(b), fast, orcl);
        return false;
    }
    return true;
}

void fuzz_no_overflow_mul() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyMul);
        if (!fuzz_pair_mul<std::uint8_t>(ctr[0], ctr[1])) fail("no_overflow_mul<u8>", i, "see preceding line");
        if (!fuzz_pair_mul<std::uint16_t>(ctr[0], ctr[1])) fail("no_overflow_mul<u16>", i, "see preceding line");
        if (!fuzz_pair_mul<std::uint32_t>(ctr[0], ctr[1])) fail("no_overflow_mul<u32>", i, "see preceding line");
        if (!fuzz_pair_mul<std::uint64_t>(ctr[0], ctr[1])) fail("no_overflow_mul<u64>", i, "see preceding line");
        if (!fuzz_pair_mul<std::int8_t>(ctr[0], ctr[1])) fail("no_overflow_mul<i8>", i, "see preceding line");
        if (!fuzz_pair_mul<std::int16_t>(ctr[0], ctr[1])) fail("no_overflow_mul<i16>", i, "see preceding line");
        if (!fuzz_pair_mul<std::int32_t>(ctr[0], ctr[1])) fail("no_overflow_mul<i32>", i, "see preceding line");
        if (!fuzz_pair_mul<std::int64_t>(ctr[0], ctr[1])) fail("no_overflow_mul<i64>", i, "see preceding line");
        g_sink ^= static_cast<int>(ctr[0] ^ ctr[1]);
    }
}

template <typename T>
bool fuzz_pair_sum(std::uint32_t a32, std::uint32_t b32) {
    auto const a = static_cast<T>(a32);
    auto const b = static_cast<T>(b32);
    bool const fast = dc::no_overflow_sum<T>(a, b);
    bool const orcl = dco::no_overflow_sum_oracle<T>(a, b);
    if (fast != orcl) {
        std::fprintf(stderr, "  T=%s a=%lld b=%lld fast=%d oracle=%d\n", std::is_signed_v<T> ? "signed" : "unsigned",
                     static_cast<long long>(a), static_cast<long long>(b), fast, orcl);
        return false;
    }
    return true;
}

void fuzz_no_overflow_sum() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeySum);
        if (!fuzz_pair_sum<std::uint8_t>(ctr[0], ctr[1])) fail("no_overflow_sum<u8>", i, "see preceding line");
        if (!fuzz_pair_sum<std::uint16_t>(ctr[0], ctr[1])) fail("no_overflow_sum<u16>", i, "see preceding line");
        if (!fuzz_pair_sum<std::uint32_t>(ctr[0], ctr[1])) fail("no_overflow_sum<u32>", i, "see preceding line");
        if (!fuzz_pair_sum<std::uint64_t>(ctr[0], ctr[1])) fail("no_overflow_sum<u64>", i, "see preceding line");
        if (!fuzz_pair_sum<std::int8_t>(ctr[0], ctr[1])) fail("no_overflow_sum<i8>", i, "see preceding line");
        if (!fuzz_pair_sum<std::int16_t>(ctr[0], ctr[1])) fail("no_overflow_sum<i16>", i, "see preceding line");
        if (!fuzz_pair_sum<std::int32_t>(ctr[0], ctr[1])) fail("no_overflow_sum<i32>", i, "see preceding line");
        if (!fuzz_pair_sum<std::int64_t>(ctr[0], ctr[1])) fail("no_overflow_sum<i64>", i, "see preceding line");
        g_sink ^= static_cast<int>(ctr[2] ^ ctr[3]);
    }
}

// The bounds are drawn from the same narrow range as the elements, so
// the predicate answers both ways often enough to be informative.

void fuzz_all_in_range() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyAllInRange);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        std::uint16_t buf[kMaxSpanLen]{};
        for (std::size_t k = 0; k < len; ++k) {
            // Re-seeding per element keeps the stream deterministic
            // without drawing a fresh block for every element.
            auto const sub = crucible::Philox::generate(static_cast<std::uint64_t>(i) * 64 + k, kKeyAllInRange);
            buf[k] = static_cast<std::uint16_t>(sub[0] & 0x0FFFu);
        }
        std::uint16_t const lo = static_cast<std::uint16_t>(ctr[1] & 0x0FFFu);
        std::uint16_t const hi = static_cast<std::uint16_t>(ctr[2] & 0x0FFFu);
        std::span<const std::uint16_t> xs{buf, len};
        bool const fast = dc::all_in_range<std::uint16_t>(xs, lo, hi);
        bool const orcl = dco::all_in_range_oracle<std::uint16_t>(xs, lo, hi);
        if (fast != orcl) {
            std::fprintf(stderr, "  len=%zu lo=%u hi=%u fast=%d oracle=%d\n", len, unsigned{lo}, unsigned{hi}, fast,
                         orcl);
            fail("all_in_range", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[0]);
    }
}

void fuzz_strictly_increasing() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyStrictInc);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        std::int16_t buf[kMaxSpanLen]{};
        // Noise almost never happens to be ordered, so half the
        // rounds build an ordered sequence on purpose.  Otherwise the
        // accepting branch would hardly ever run.
        bool const make_monotone = (ctr[0] & 1u) == 1u;
        std::int16_t prev = -8'000;
        for (std::size_t k = 0; k < len; ++k) {
            auto const sub = crucible::Philox::generate(static_cast<std::uint64_t>(i) * 64 + k, kKeyStrictInc);
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
            std::fprintf(stderr, "  len=%zu monotone=%d fast=%d oracle=%d\n", len, make_monotone ? 1 : 0, fast, orcl);
            fail("strictly_increasing", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[1]);
    }
}

void fuzz_weakly_increasing() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyWeakInc);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        std::int16_t buf[kMaxSpanLen]{};
        bool const make_monotone = (ctr[0] & 1u) == 1u;
        std::int16_t prev = -8'000;
        for (std::size_t k = 0; k < len; ++k) {
            auto const sub = crucible::Philox::generate(static_cast<std::uint64_t>(i) * 64 + k, kKeyWeakInc);
            if (make_monotone) {
                // A delta of zero repeats the previous element, which
                // is what separates this predicate from the strict
                // one.  Drawing from a small range makes that happen
                // often.
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
            std::fprintf(stderr, "  len=%zu monotone=%d fast=%d oracle=%d\n", len, make_monotone ? 1 : 0, fast, orcl);
            fail("weakly_increasing", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[2]);
    }
}

void fuzz_is_power_of_two_le() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyPow2Le);
        // A random word is almost never a power of two, so half the
        // rounds replace it with one.
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
                std::fprintf(stderr, "  T=u32 x=%u bound=%u fast=%d oracle=%d\n", unsigned{x}, unsigned{bound}, fast,
                             orcl);
                fail("is_power_of_two_le<u32>", i, "see preceding line");
            }
        }
        // The signed width also reaches the non-positive rejection,
        // which the unsigned one cannot.
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
                std::fprintf(stderr, "  T=i32 x=%d bound=%d fast=%d oracle=%d\n", int{x}, int{bound}, fast, orcl);
                fail("is_power_of_two_le<i32>", i, "see preceding line");
            }
        }
        g_sink ^= static_cast<int>(ctr[0] ^ ctr[3]);
    }
}

// The length starts at one here.  What this predicate should answer
// for an empty list of factors is an open question, settled by named
// compile-time witnesses rather than left to the harness.

void fuzz_factorization_eq() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyFactorEq);
        std::size_t const len = 1u + (ctr[0] % kMaxSpanLen);
        std::uint32_t buf[kMaxSpanLen]{};
        // Factors below 32 give a sixteen-element product that
        // overflows the narrow accumulator without doing so on the
        // first few elements, so both the accepting and the
        // overflow-rejecting paths run.
        std::uint32_t fast_product = 1;
        bool fast_overflowed = false;
        for (std::size_t k = 0; k < len; ++k) {
            auto const sub = crucible::Philox::generate(static_cast<std::uint64_t>(i) * 64 + k, kKeyFactorEq);
            std::uint32_t const f = (sub[0] % 32u) + 1u;
            buf[k] = f;
            // The wide running product is what lets the round choose
            // a total that is right or wrong on purpose.
            std::uint64_t const honest = static_cast<std::uint64_t>(fast_product) * f;
            if (honest > std::numeric_limits<std::uint32_t>::max()) {
                fast_overflowed = true;
            }
            fast_product = static_cast<std::uint32_t>(honest & 0xFFFF'FFFFu);
        }
        std::uint32_t total = fast_product;
        if ((ctr[1] & 1u) == 1u) {
            total ^= ctr[2] | 1u;  // a non-zero xor is always wrong
        }
        // Once the wide product has overflowed the narrow type, the
        // truncated value is no longer the product of anything, so the
        // total is wrong whatever it is.
        if (fast_overflowed) {
            total = ctr[2];
        }
        std::span<const std::uint32_t> factors{buf, len};
        bool const fast = dc::factorization_eq<std::uint32_t>(factors, total);
        bool const orcl = dco::factorization_eq_oracle<std::uint32_t>(factors, total);
        if (fast != orcl) {
            std::fprintf(stderr, "  len=%zu total=%u overflowed=%d fast=%d oracle=%d\n", len, unsigned{total},
                         fast_overflowed ? 1 : 0, fast, orcl);
            fail("factorization_eq", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(total);
    }
}

// Two random words are usually coprime, so half the rounds multiply
// both by a shared small factor and are usually not.

template <typename T>
bool fuzz_pair_coprime(std::uint32_t a32, std::uint32_t b32, std::uint32_t bias) {
    auto a = static_cast<T>(a32);
    auto b = static_cast<T>(b32);
    if ((bias & 1u) == 1u) {
        std::uint32_t const f = (bias >> 1) % 7u + 2u;  // a shared factor in [2, 8)
        a = static_cast<T>(static_cast<std::uint64_t>(a) * f);
        b = static_cast<T>(static_cast<std::uint64_t>(b) * f);
    }
    bool const fast = dc::coprime<T>(a, b);
    bool const orcl = dco::coprime_oracle<T>(a, b);
    if (fast != orcl) {
        std::fprintf(stderr, "  T=%s a=%lld b=%lld fast=%d oracle=%d\n", std::is_signed_v<T> ? "signed" : "unsigned",
                     static_cast<long long>(a), static_cast<long long>(b), fast, orcl);
        return false;
    }
    return true;
}

void fuzz_coprime() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyCoprime);
        if (!fuzz_pair_coprime<std::uint32_t>(ctr[0], ctr[1], ctr[2])) fail("coprime<u32>", i, "see preceding line");
        if (!fuzz_pair_coprime<std::uint64_t>(ctr[0], ctr[1], ctr[2])) fail("coprime<u64>", i, "see preceding line");
        if (!fuzz_pair_coprime<std::int32_t>(ctr[0], ctr[1], ctr[2])) fail("coprime<i32>", i, "see preceding line");
        if (!fuzz_pair_coprime<std::int64_t>(ctr[0], ctr[1], ctr[2])) fail("coprime<i64>", i, "see preceding line");
        g_sink ^= static_cast<int>(ctr[3]);
    }
}

void fuzz_conjunction() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyConjunction);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        bool buf[kMaxSpanLen]{};
        for (std::size_t k = 0; k < len; ++k) {
            // Under a fair coin the accepting case would appear once
            // in two to the length, which at these lengths is almost
            // never.  Three-to-one in favour of true fixes that.
            auto const sub = crucible::Philox::generate(static_cast<std::uint64_t>(i) * 64 + k, kKeyConjunction);
            buf[k] = (sub[0] & 0x3u) != 0u;
        }
        std::span<const bool> xs{buf, len};
        bool const fast = dc::conjunction(xs);
        bool const orcl = dco::conjunction_oracle(xs);
        if (fast != orcl) {
            std::fprintf(stderr, "  len=%zu fast=%d oracle=%d\n", len, fast, orcl);
            fail("conjunction", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[1]);
    }
}

void fuzz_disjunction() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyDisjunction);
        std::size_t const len = ctr[0] % (kMaxSpanLen + 1);
        bool buf[kMaxSpanLen]{};
        for (std::size_t k = 0; k < len; ++k) {
            // The mirror of the bias above, for the same reason: the
            // rejecting case is the rare one here.
            auto const sub = crucible::Philox::generate(static_cast<std::uint64_t>(i) * 64 + k, kKeyDisjunction);
            buf[k] = (sub[0] & 0x3u) == 0u;
        }
        std::span<const bool> xs{buf, len};
        bool const fast = dc::disjunction(xs);
        bool const orcl = dco::disjunction_oracle(xs);
        if (fast != orcl) {
            std::fprintf(stderr, "  len=%zu fast=%d oracle=%d\n", len, fast, orcl);
            fail("disjunction", i, "see preceding line");
        }
        g_sink ^= static_cast<int>(ctr[1]);
    }
}

// Four clauses, and each has to get its turn at being the one that
// rejects.  Half the rounds pass a zero alignment, which reaches the
// guard against dividing by it; the rest pass a power of two.  Half
// the values are built as multiples of the alignment.  The bounds are
// drawn narrow so the value falls inside and outside about equally
// often.

void fuzz_aligned_in_range() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = crucible::Philox::generate(static_cast<std::uint64_t>(i), kKeyAlignInRange);
        std::uint64_t alignment;
        if ((ctr[0] & 1u) == 1u) {
            alignment = 0;
        } else {
            int const which = static_cast<int>(ctr[0] >> 1) % 8;
            alignment = std::uint64_t{1} << which;  // one of 1 through 128
        }
        std::uint64_t value;
        if ((ctr[1] & 1u) == 1u && alignment != 0u) {
            value = (static_cast<std::uint64_t>(ctr[2]) % 1024u) * alignment;
        } else {
            value = ctr[2] & 0xFFFFu;
        }
        std::uint64_t const lo = (ctr[3] >> 16) & 0xFFu;  // in [0, 256)
        std::uint64_t const hi = lo + (ctr[3] & 0x0FFFu);  // in [lo, lo + 4096)
        bool const fast = dc::aligned_in_range(value, lo, hi, alignment);
        bool const orcl = dco::aligned_in_range_oracle(value, lo, hi, alignment);
        if (fast != orcl) {
            std::fprintf(stderr, "  value=%llu low=%llu high=%llu align=%llu fast=%d oracle=%d\n",
                         static_cast<unsigned long long>(value), static_cast<unsigned long long>(lo),
                         static_cast<unsigned long long>(hi), static_cast<unsigned long long>(alignment), fast, orcl);
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
        // Every predicate xors something into the sink, so a sink of
        // exactly zero means the loops were skipped or deleted rather
        // than run.
        std::fprintf(stderr, "test_decide_fuzz: WARNING — sink is 0; harness may have been DCE'd\n");
        return 1;
    }
    std::fprintf(stderr, "test_decide_fuzz: all %d × 11 = %d iterations passed\n", kIterations, kIterations * 11);
    return 0;
}
