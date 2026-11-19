// Tests of foundation/core/Text.h and foundation/core/Format.h: a TextView
// compares its characters, a FixedText holds at most its capacity, and
// format() writes each kind of argument and refuses a text that does not
// fit.

#include <foundation/core/Format.h>
#include <foundation/core/Region.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::FixedText;
using ::foundation::core::format;
using ::foundation::core::TextView;
using ::foundation::core::Full;

// The text of a format() that fits its buffer.
[[nodiscard]] TextView text_of(::foundation::core::Result<TextView, Full<char>> result) {
    return std::move(result).expect("the text fits the buffer of the test");
}

void test_text_view_compares_its_characters() {
    TextView const queue{"queue"};
    CRUCIBLE_TEST_ASSERT(kTest, queue.size() == 5);
    CRUCIBLE_TEST_ASSERT(kTest, queue == TextView{"queue"});
    CRUCIBLE_TEST_ASSERT(kTest, !(queue == TextView{"queues"}));
    CRUCIBLE_TEST_ASSERT(kTest, !(queue == TextView{"quell"}));
    CRUCIBLE_TEST_ASSERT(kTest, TextView{} == TextView{""});
    CRUCIBLE_TEST_ASSERT(kTest, TextView{}.size() == 0);
}

void test_fixed_text_starts_empty() {
    FixedText<8> const text;
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, text.view() == TextView{""});
    CRUCIBLE_TEST_ASSERT(kTest, FixedText<8>::capacity == 8);
}

// The loop reads each character of a text one time, in order, and a window
// gives the characters from its offset.
void test_text_view_reads_its_characters() {
    TextView const list{"0-3,8"};
    std::size_t digits = 0;
    std::size_t position = 0;
    for (char const character : list) {
        if (character >= '0' && character <= '9') ++digits;
        ++position;
    }
    CRUCIBLE_TEST_ASSERT(kTest, digits == 3 && position == 5);
    CRUCIBLE_TEST_ASSERT(kTest, list.window(2, 3).expect("the window fits") == TextView{"3,8"});
    CRUCIBLE_TEST_ASSERT(kTest, list.window(5, 0).expect("the empty window fits") == TextView{});
    CRUCIBLE_TEST_ASSERT(kTest, list.window(5, 1).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, list.window(6, 0).is_none());
    int turns = 0;
    for ([[maybe_unused]] char const character : TextView{}) {
        ++turns;
    }
    CRUCIBLE_TEST_ASSERT(kTest, turns == 0);
}

// A FixedText gives the loop over the characters that it holds, and clear()
// empties it.
void test_fixed_text_loop_and_clear() {
    FixedText<16> text;
    CRUCIBLE_TEST_ASSERT(kTest, format(text, "{}{}", 12, TextView{"ab"}).is_ok());
    std::size_t count = 0;
    for ([[maybe_unused]] char const character : text) {
        ++count;
    }
    CRUCIBLE_TEST_ASSERT(kTest, count == 4);
    text.clear();
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 0 && text.view() == TextView{});
}

// A FixedText from a literal, and from append() of a character and of a text
// at run time.  A refused write keeps the characters of the buffer.
void test_fixed_text_builds_from_characters() {
    FixedText<8> text{"cub"};
    char volatile letter = 'i';
    CRUCIBLE_FATAL_INVARIANT(text.append(letter).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, text.append(TextView{"c"}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, text.view() == TextView{"cubic"});
    CRUCIBLE_TEST_ASSERT(kTest, text.append(TextView{"_wide"}).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, text.view() == TextView{"cubic"});
    CRUCIBLE_TEST_ASSERT(kTest, text.append(TextView{"bbr"}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 8);
    CRUCIBLE_FATAL_INVARIANT(text.append(letter).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, text.view() == TextView{"cubicbbr"});
}

enum class Lane : std::uint8_t {
    Send,
    ReceiveQueue,
};

// The name of an enumerator comes from a value that only the run knows, so
// the call runs at run time.  A value that no enumerator holds gives the
// sentinel.
void test_enum_name_text_at_run_time() {
    std::uint8_t volatile raw = 1;
    Lane const queue = static_cast<Lane>(raw);
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::enum_name_text(queue) == TextView{"ReceiveQueue"});
    CRUCIBLE_TEST_ASSERT(kTest, (::foundation::core::enum_words_text<Lane, '_'>(queue) == TextView{"receive_queue"}));
    raw = 7;
    Lane const unknown = static_cast<Lane>(raw);
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::enum_name_text(unknown) == TextView{"<unknown Lane>"});
    CRUCIBLE_TEST_ASSERT(kTest,
                         (::foundation::core::enum_words_text<Lane, '-'>(unknown) == TextView{"<unknown Lane>"}));
}

// A cursor of a text never leaves the text: a read at the end and a step
// past the end end the process in each build.  Each one runs in a child
// through expect_dies of the Check family.
void test_text_cursor_at_the_end_aborts(::fixy::TestRunnerCtx const& test) {
    TextView const pair{"ab"};
    ::fixy::check::expect_dies(
        test, [&pair] noexcept { static_cast<void>(*pair.end()); },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a View"});
    ::fixy::check::expect_dies(
        test,
        [&pair] noexcept {
            auto cursor = pair.begin();
            ++cursor;
            ++cursor;
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a View"});
}

void test_format_writes_integers_in_decimal() {
    FixedText<96> text;
    CRUCIBLE_TEST_ASSERT(kTest,
                         text_of(format(text, "{} {} {} {}", 0, 7, -7, 123456789)) == TextView{"0 7 -7 123456789"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{} {}", INT64_MIN, INT64_MAX))
                                    == TextView{"-9223372036854775808 9223372036854775807"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{}", UINT64_MAX)) == TextView{"18446744073709551615"});
    std::int8_t const small_signed = -128;
    std::uint8_t const small_unsigned = 255;
    unsigned short const short_value = 65535;
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{}|{}|{}", small_signed, small_unsigned, short_value))
                                    == TextView{"-128|255|65535"});
}

void test_format_writes_bools_and_texts() {
    FixedText<64> text;
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{} {}", true, false)) == TextView{"true false"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "[{}]", TextView{"the queue"})) == TextView{"[the queue]"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "[{}]", TextView{})) == TextView{"[]"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{} holds {} of {}", TextView{"ring"}, 3, 4))
                                    == TextView{"ring holds 3 of 4"});
}

// A brace in an argument is written as it is, and `{{` and `}}` write one
// brace each.
void test_format_writes_escapes_and_braces_of_arguments() {
    FixedText<64> text;
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{{}}")) == TextView{"{}"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{{{}}}", 5)) == TextView{"{5}"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{}", TextView{"{}"})) == TextView{"{}"});
}

// A text that fills the buffer exactly fits.  A text one character longer
// gives Full<char>, and the buffer holds the first characters of the text.
void test_format_refuses_a_text_that_does_not_fit() {
    FixedText<5> text;
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{}", 12345)) == TextView{"12345"});
    auto const longer = format(text, "{}", 123456);
    CRUCIBLE_TEST_ASSERT(kTest, longer.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 5);
    CRUCIBLE_TEST_ASSERT(kTest, text.view() == TextView{"12345"});
    auto const much_longer = format(text, "the queue holds {} items", 3);
    CRUCIBLE_TEST_ASSERT(kTest, much_longer.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, text.view() == TextView{"the q"});
}

// A second format() writes from the start of the buffer.
void test_format_writes_from_the_start() {
    FixedText<16> text;
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "a long text")) == TextView{"a long text"});
    CRUCIBLE_TEST_ASSERT(kTest, text_of(format(text, "{}", 1)) == TextView{"1"});
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 1);
}

// A text longer than one window of the cold formatter keeps each byte.
void test_format_keeps_a_long_text() {
    FixedText<1200> text;
    TextView const tens{"0123456789"};
    auto const result = format(text, "{}{}{}{}{}{}{}{}{}{}|{}{}{}{}{}{}{}{}{}{}", tens, tens, tens, tens, tens, tens,
                               tens, tens, tens, tens, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    CRUCIBLE_TEST_ASSERT(kTest, result.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 112);
}

}  // namespace

int main() {
    test_text_view_compares_its_characters();
    test_fixed_text_starts_empty();
    test_text_view_reads_its_characters();
    test_fixed_text_loop_and_clear();
    test_fixed_text_builds_from_characters();
    test_enum_name_text_at_run_time();
    test_text_cursor_at_the_end_aborts(::fixy::TestRunnerCtx{::foundation::effects::testing::test()});
    test_format_writes_integers_in_decimal();
    test_format_writes_bools_and_texts();
    test_format_writes_escapes_and_braces_of_arguments();
    test_format_refuses_a_text_that_does_not_fit();
    test_format_writes_from_the_start();
    test_format_keeps_a_long_text();
    ::fixy::check::pass(kTest, "test_core_text: all tests passed\n");
    return 0;
}
