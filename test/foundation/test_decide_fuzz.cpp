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

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/contracts/Decide.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace dc = foundation::decide;
namespace dco = decide_oracle;

// One block of four words for one (counter, key) pair comes from the
// Philox4x32-10 generator of the base.  The counter holds the iteration in
// its two low words, and the key is the key of 64 bits, so a pair names one
// block on every machine.
using Block = ::fixy::Philox4x32::Counter;
using ::fixy::index;

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

// Ends the test with the name of the procedure and the iteration.  The line
// before it holds the inputs and the two answers.
[[noreturn]] void fail(::fixy::TextView proc, int iter) noexcept {
    ::fixy::fatal("test_decide_fuzz: MISMATCH in {} at iteration {}: {}", proc, iter,
                  ::fixy::TextView{"see preceding line"});
}

// The same, for a procedure swept over a width: the width is named by
// its sign and its bit count, as `no_overflow_sum<i16>`.
template <typename T>
[[noreturn]] void fail_width(::fixy::TextView proc, int iter) noexcept {
    ::fixy::TextView const sign = std::is_signed_v<T> ? ::fixy::TextView{"i"} : ::fixy::TextView{"u"};
    ::fixy::fatal("test_decide_fuzz: MISMATCH in {}<{}{}> at iteration {}: {}", proc, sign, sizeof(T) * 8, iter,
                  ::fixy::TextView{"see preceding line"});
}

// The widths the sum is swept over.  The narrow signed widths are the
// point of the sweep: a hand-rolled check that widened one operand and
// not the other would agree with the compiler's on the wide types and
// disagree here.  The roster is a Pack, the walk below folds over its
// types, and it is the only place the widths are listed.
using PairWidths = ::fixy::Pack<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, std::int8_t, std::int16_t,
                                std::int32_t, std::int64_t>;

// Calls check.operator()<T>() for each T in the roster, in order.
template <typename Roster>
struct WidthWalk;

template <typename... Widths>
struct WidthWalk<::fixy::Pack<Widths...>> {
    template <typename Check>
    static void run(Check& check) {
        (check.template operator()<Widths>(), ...);
    }
};

template <typename T>
bool fuzz_sum_pair(std::uint32_t a32, std::uint32_t b32) {
    auto const a = static_cast<T>(a32);
    auto const b = static_cast<T>(b32);
    bool const fast = dc::no_overflow_sum(a, b);
    bool const orcl = dco::no_overflow_sum_oracle(a, b);
    if (fast != orcl) {
        ::fixy::TextView const sign = std::is_signed_v<T> ? ::fixy::TextView{"signed"} : ::fixy::TextView{"unsigned"};
        ::fixy::report(::fixy::Sink::Err, "  T={} a={} b={} fast={} oracle={}\n", sign, static_cast<long long>(a),
                       static_cast<long long>(b), int{fast}, int{orcl});
        return false;
    }
    return true;
}

void fuzz_no_overflow_sum() {
    for (int i = 0; i < kIterations; ++i) {
        Block const ctr = ::fixy::Philox4x32::generate(static_cast<std::uint64_t>(i), kKeySum);
        auto check = [&]<typename T>() {
            if (!fuzz_sum_pair<T>(ctr.at(index<0>()), ctr.at(index<1>()))) fail_width<T>("no_overflow_sum", i);
        };
        WidthWalk<PairWidths>::run(check);
        g_sink ^= static_cast<int>(ctr.at(index<2>()) ^ ctr.at(index<3>()));
    }
}

void fuzz_is_power_of_two_le() {
    for (int i = 0; i < kIterations; ++i) {
        Block const ctr = ::fixy::Philox4x32::generate(static_cast<std::uint64_t>(i), kKeyPow2Le);
        // A random word is almost never a power of two, so half the
        // rounds replace it with one.
        {
            std::uint32_t x = ctr.at(index<0>());
            if ((ctr.at(index<2>()) & 1u) == 1u && x != 0u) {
                int const which = static_cast<int>(ctr.at(index<3>()) % 32);
                x = std::uint32_t{1} << which;
            }
            std::uint32_t const bound = ctr.at(index<1>());
            bool const fast = dc::is_power_of_two_le<std::uint32_t>(x, bound);
            bool const orcl = dco::is_power_of_two_le_oracle<std::uint32_t>(x, bound);
            if (fast != orcl) {
                ::fixy::report(::fixy::Sink::Err, "  T=u32 x={} bound={} fast={} oracle={}\n", unsigned{x},
                               unsigned{bound}, int{fast}, int{orcl});
                fail("is_power_of_two_le<u32>", i);
            }
        }
        // The signed width also reaches the non-positive rejection,
        // which the unsigned one cannot.
        {
            std::int32_t x = static_cast<std::int32_t>(ctr.at(index<0>()));
            if ((ctr.at(index<2>()) & 2u) == 2u && x > 0) {
                int const which = static_cast<int>(ctr.at(index<3>()) % 31);
                x = std::int32_t{1} << which;
            }
            std::int32_t const bound = static_cast<std::int32_t>(ctr.at(index<1>()));
            bool const fast = dc::is_power_of_two_le<std::int32_t>(x, bound);
            bool const orcl = dco::is_power_of_two_le_oracle<std::int32_t>(x, bound);
            if (fast != orcl) {
                ::fixy::report(::fixy::Sink::Err, "  T=i32 x={} bound={} fast={} oracle={}\n", int{x}, int{bound},
                               int{fast}, int{orcl});
                fail("is_power_of_two_le<i32>", i);
            }
        }
        g_sink ^= static_cast<int>(ctr.at(index<0>()) ^ ctr.at(index<3>()));
    }
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Err,
                   "test_decide_fuzz: {} iterations × 2 procedures "
                   "(Philox4x32-10 stream, fixed seed)\n",
                   kIterations);

    fuzz_no_overflow_sum();
    fuzz_is_power_of_two_le();

    if (g_sink == 0) {
        // Every predicate xors something into the sink, so a sink of
        // exactly zero means the loops were skipped or deleted rather
        // than run.
        ::fixy::report(::fixy::Sink::Err, "test_decide_fuzz: WARNING — sink is 0; harness may have been DCE'd\n");
        return 1;
    }
    ::fixy::check::pass(kTest, "test_decide_fuzz: all {} × 2 = {} iterations passed\n", kIterations, kIterations * 2);
    return 0;
}
