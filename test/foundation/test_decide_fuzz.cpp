// SPDX-License-Identifier: Apache-2.0
//
// Every predicate here is checked against a reference oracle that
// reaches the same answer by a different route: widening into a larger
// integer rather than asking the compiler about overflow, and counting set
// bits rather than clearing the lowest one.  The difference in algorithm is what
// makes the check worth anything.  An oracle that restated the
// production body would agree with it about every bug it has.
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
// widening, the wrong sign on a termination test.
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

#include "decide_oracle.h"

#include <foundation/contracts/Decide.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <meta>
#include <random>
#include <tuple>
#include <type_traits>
#include <utility>
#include "../test_assert.h"

namespace {

namespace dc = foundation::decide;
namespace dco = decide_oracle;

// One block of four words for one (counter, key) pair, from the standard
// Philox4x32-10 engine.  The key seeds the engine through a seed sequence
// and the counter is set directly, so a pair names one block on every
// machine.
[[nodiscard]] std::array<std::uint32_t, 4> generate(std::uint64_t counter, std::uint64_t key) {
    std::seed_seq key_words{static_cast<std::uint32_t>(key), static_cast<std::uint32_t>(key >> 32)};
    std::philox4x32 engine{key_words};
    engine.set_counter({static_cast<std::uint32_t>(counter), static_cast<std::uint32_t>(counter >> 32), 0u, 0u});
    std::array<std::uint32_t, 4> block{};
    for (std::uint32_t& word : block) {
        word = static_cast<std::uint32_t>(engine());
    }
    return block;
}

// Raising this is free.  Lowering it weakens the statistical claim.
constexpr int kIterations = 10000;

// The keys themselves are arbitrary.  What matters is that they
// differ, so that the streams are independent and a failure points at
// one predicate.

constexpr std::uint64_t kKeySum = 0xC001C0DE00020002ULL;
constexpr std::uint64_t kKeyPow2Le = 0xC001C0DE00060006ULL;

// Both calls are pure functions of pure inputs and the comparison
// feeds nothing, so without a volatile sink the optimizer is entitled
// to delete the whole loop.

volatile int g_sink = 0;

[[noreturn]] void fail(const char* proc, int iter, const char* msg) {
    std::fprintf(stderr, "test_decide_fuzz: MISMATCH in %s at iteration %d: %s\n", proc, iter, msg);
    std::exit(1);
}

// The same, for a procedure swept over a width: the width is named by
// its sign and its bit count, as `no_overflow_sum<i16>`.
template <typename T>
[[noreturn]] void fail_width(const char* proc, int iter) {
    char labelled[64];
    std::snprintf(labelled, sizeof(labelled), "%s<%c%zu>", proc, std::is_signed_v<T> ? 'i' : 'u', sizeof(T) * 8);
    fail(labelled, iter, "see preceding line");
}

// The widths the sum is swept over.  The narrow signed widths are the
// point of the sweep: a hand-rolled check that widened one operand and
// not the other would agree with the compiler's on the wide types and
// disagree here.  The roster is a tuple so the walk below can read it by
// reflection, and it is the only place the widths are listed.
using PairWidths = std::tuple<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, std::int8_t, std::int16_t,
                              std::int32_t, std::int64_t>;

// Calls check.operator()<T>() for each T in the roster, in order.
template <typename Roster, typename Check>
void for_each_width(Check&& check) {
    static constexpr auto widths = std::define_static_array(std::meta::template_arguments_of(^^Roster));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto width : widths) {
        using T = [:width:];
        check.template operator()<T>();
    }
#pragma GCC diagnostic pop
}

template <typename T>
bool fuzz_sum_pair(std::uint32_t a32, std::uint32_t b32) {
    auto const a = static_cast<T>(a32);
    auto const b = static_cast<T>(b32);
    bool const fast = dc::no_overflow_sum(a, b);
    bool const orcl = dco::no_overflow_sum_oracle(a, b);
    if (fast != orcl) {
        std::fprintf(stderr, "  T=%s a=%lld b=%lld fast=%d oracle=%d\n", std::is_signed_v<T> ? "signed" : "unsigned",
                     static_cast<long long>(a), static_cast<long long>(b), fast, orcl);
        return false;
    }
    return true;
}

void fuzz_no_overflow_sum() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = generate(static_cast<std::uint64_t>(i), kKeySum);
        for_each_width<PairWidths>([&]<typename T>() {
            if (!fuzz_sum_pair<T>(ctr[0], ctr[1])) fail_width<T>("no_overflow_sum", i);
        });
        g_sink ^= static_cast<int>(ctr[2] ^ ctr[3]);
    }
}

void fuzz_is_power_of_two_le() {
    for (int i = 0; i < kIterations; ++i) {
        auto const ctr = generate(static_cast<std::uint64_t>(i), kKeyPow2Le);
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

}  // namespace

int main() {
    std::fprintf(stderr,
                 "test_decide_fuzz: %d iterations × 2 procedures "
                 "(Philox4x32-10 stream, fixed seed)\n",
                 kIterations);

    fuzz_no_overflow_sum();
    fuzz_is_power_of_two_le();

    if (g_sink == 0) {
        // Every predicate xors something into the sink, so a sink of
        // exactly zero means the loops were skipped or deleted rather
        // than run.
        std::fprintf(stderr, "test_decide_fuzz: WARNING — sink is 0; harness may have been DCE'd\n");
        return 1;
    }
    crucible::test::pass("test_decide_fuzz: all {} × 2 = {} iterations passed\n", kIterations, kIterations * 2);
    return 0;
}
