// Tests of foundation/core/LendCount.h: the lends nest, a count of one byte
// holds 255 lends and ends the process at one more, and a count of four bytes
// holds more.

#include <foundation/core/LendCount.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// Opens depth lends of the count, one in the body of the other, and tells that
// the count is lent in each of them.
template <class Word>
[[nodiscard]] bool is_lent_at_each_depth(::fixy::LendCount<Word>& count, std::uint32_t depth) noexcept {
    if (depth == 0) return true;
    [[maybe_unused]] auto const lend = count.lend();
    return count.is_lent() && is_lent_at_each_depth(count, depth - 1);
}

void test_the_count_is_zero_after_the_last_lend() {
    ::fixy::LendCount<unsigned char> narrow{};
    CRUCIBLE_TEST_ASSERT(kTest, !narrow.is_lent());
    CRUCIBLE_TEST_ASSERT(kTest, is_lent_at_each_depth(narrow, 3) && !narrow.is_lent());
    ::fixy::LendCount<std::uint32_t> wide{};
    CRUCIBLE_TEST_ASSERT(kTest, is_lent_at_each_depth(wide, 3) && !wide.is_lent());
}

// A count of one byte would wrap to zero at 256 lends, and a change door would
// find no open lend.  So the 256th lend ends the process.  A count of four
// bytes takes more.
void test_a_full_word_ends_the_process() {
    ::fixy::LendCount<unsigned char> narrow{};
    CRUCIBLE_TEST_ASSERT(kTest, is_lent_at_each_depth(narrow, 255) && !narrow.is_lent());
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            ::fixy::LendCount<unsigned char> full{};
            static_cast<void>(is_lent_at_each_depth(full, 256));
        },
        ::fixy::TextView{"more frame doors lend what one owner holds at one time than the word of its LendCount can "
                         "count"});
    ::fixy::LendCount<std::uint32_t> wide{};
    CRUCIBLE_TEST_ASSERT(kTest, is_lent_at_each_depth(wide, 300) && !wide.is_lent());
}

}  // namespace

int main() {
    test_the_count_is_zero_after_the_last_lend();
    test_a_full_word_ends_the_process();
    ::fixy::check::pass(kTest, "test_core_lend_count: all tests passed\n");
    return 0;
}
