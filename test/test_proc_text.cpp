// The number reads of crucible/perf/ProcText.h, which read the counts of /proc
// text.  A count that does not fit 64 bits gives no value, and so do a sum and
// a product of counts.  The old reads of ProcGauges accumulated with no check,
// so a count of 21 digits, a slab product and the hit ratio of a large NUMA
// count wrapped to a wrong reading.

#include <crucible/perf/ProcText.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

namespace proc_text = crucible::perf::proc_text;

// The value of the decimal read at the start of text, and the characters
// after it.
struct Taken {
    ::fixy::Option<std::uint64_t> value{};
    ::fixy::TextView rest{};
};

[[nodiscard]] Taken taken_decimal(::fixy::TextView text) noexcept {
    ::fixy::Option<std::uint64_t> value = proc_text::take_decimal(text);
    return Taken{.value = value, .rest = text};
}

[[nodiscard]] Taken taken_hex(::fixy::TextView text) noexcept {
    ::fixy::Option<std::uint64_t> value = proc_text::take_hex(text);
    return Taken{.value = value, .rest = text};
}

[[nodiscard]] bool holds(::fixy::Option<std::uint64_t> const& value, std::uint64_t expected) noexcept {
    return value.is_some_and([expected](std::uint64_t given) noexcept { return given == expected; });
}

void test_decimal_reads() {
    const Taken plain = taken_decimal(::fixy::TextView{"  4096 rest"});
    CRUCIBLE_TEST_ASSERT(kTest, holds(plain.value, 4096) && plain.rest == ::fixy::TextView{" rest"});
    CRUCIBLE_TEST_ASSERT(kTest, holds(taken_decimal(::fixy::TextView{"word"}).value, 0)
                                    && "a text with no digit reads as zero");
    CRUCIBLE_TEST_ASSERT(
        kTest, holds(taken_decimal(::fixy::TextView{"18446744073709551615"}).value, ::fixy::max_of<std::uint64_t>));
    const Taken wide = taken_decimal(::fixy::TextView{"123456789012345678901 rest"});
    CRUCIBLE_TEST_ASSERT(kTest, wide.value.is_none() && "a count of 21 digits does not fit 64 bits");
    CRUCIBLE_TEST_ASSERT(kTest, wide.rest == ::fixy::TextView{" rest"} && "the read moves past each digit");
    CRUCIBLE_TEST_ASSERT(kTest, taken_decimal(::fixy::TextView{"18446744073709551616"}).value.is_none());
    ::fixy::check::pass(kTest, "  decimal_reads: PASSED\n");
}

void test_hex_reads() {
    const Taken counter = taken_hex(::fixy::TextView{"00ff3a:rest"});
    CRUCIBLE_TEST_ASSERT(kTest, holds(counter.value, 0xff3a) && counter.rest == ::fixy::TextView{":rest"});
    CRUCIBLE_TEST_ASSERT(kTest,
                         holds(taken_hex(::fixy::TextView{"FFFFFFFFFFFFFFFF"}).value, ::fixy::max_of<std::uint64_t>));
    const Taken wide = taken_hex(::fixy::TextView{"1FFFFFFFFFFFFFFFF "});
    CRUCIBLE_TEST_ASSERT(kTest, wide.value.is_none() && "17 hexadecimal digits do not fit 64 bits");
    CRUCIBLE_TEST_ASSERT(kTest, wide.rest == ::fixy::TextView{" "});
    ::fixy::check::pass(kTest, "  hex_reads: PASSED\n");
}

void test_numa_hit_ratio() {
    using proc_text::NumaCounts;
    CRUCIBLE_TEST_ASSERT(kTest, proc_text::numa_hit_ratio_x100(NumaCounts{.hit = 3, .miss = 1}) == 7500);
    CRUCIBLE_TEST_ASSERT(kTest,
                         proc_text::numa_hit_ratio_x100(NumaCounts{}) == 0 && "no allocation gives the ratio zero");
    constexpr std::uint64_t kLarge = std::uint64_t{1} << 62U;
    CRUCIBLE_TEST_ASSERT(kTest, proc_text::numa_hit_ratio_x100(NumaCounts{.hit = kLarge, .miss = kLarge}) == 5000
                                    && "a numa_hit of 2^62 gives the ratio of its counts");
    CRUCIBLE_TEST_ASSERT(kTest, proc_text::numa_hit_ratio_x100(NumaCounts{.hit = kLarge, .miss = 0}) == 10000);
    CRUCIBLE_TEST_ASSERT(kTest, proc_text::numa_hit_ratio_x100(NumaCounts{.hit = ::fixy::max_of<std::uint64_t>,
                                                                          .miss = ::fixy::max_of<std::uint64_t>})
                                        == 5000
                                    && "a sum of two counts past 64 bits gives the ratio of its counts");
    ::fixy::check::pass(kTest, "  numa_hit_ratio: PASSED\n");
}

}  // namespace

int main() {
    test_decimal_reads();
    test_hex_reads();
    test_numa_hit_ratio();
    ::fixy::check::pass(kTest, "test_proc_text: all tests passed\n");
    return 0;
}
