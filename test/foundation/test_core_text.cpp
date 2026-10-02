// Tests of foundation/core/Text.h and foundation/core/Format.h: a TextView
// compares its characters, a FixedText holds at most its capacity, and
// format() writes each kind of argument and refuses a text that does not
// fit.

#include <foundation/core/Format.h>

#include "../test_assert.h"

#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::FixedText;
using ::foundation::core::format;
using ::foundation::core::TextView;
using ::foundation::core::Truncated;

// The text of a format() that fits its buffer.
[[nodiscard]] TextView text_of(::foundation::core::Result<TextView, Truncated> result) {
    return std::move(result).expect("the text fits the buffer of the test");
}

void test_text_view_compares_its_characters() {
    TextView const queue{"queue"};
    assert(queue.size() == 5);
    assert(queue == TextView{"queue"});
    assert(!(queue == TextView{"queues"}));
    assert(!(queue == TextView{"quell"}));
    assert(TextView{} == TextView{""});
    assert(TextView{}.size() == 0);
}

void test_fixed_text_starts_empty() {
    FixedText<8> const text;
    assert(text.size() == 0);
    assert(text.view() == TextView{""});
    assert(FixedText<8>::capacity == 8);
}

void test_format_writes_integers_in_decimal() {
    FixedText<96> text;
    assert(text_of(format(text, "{} {} {} {}", 0, 7, -7, 123456789)) == TextView{"0 7 -7 123456789"});
    assert(text_of(format(text, "{} {}", INT64_MIN, INT64_MAX))
           == TextView{"-9223372036854775808 9223372036854775807"});
    assert(text_of(format(text, "{}", UINT64_MAX)) == TextView{"18446744073709551615"});
    std::int8_t const small_signed = -128;
    std::uint8_t const small_unsigned = 255;
    unsigned short const short_value = 65535;
    assert(text_of(format(text, "{}|{}|{}", small_signed, small_unsigned, short_value)) == TextView{"-128|255|65535"});
}

void test_format_writes_bools_and_texts() {
    FixedText<64> text;
    assert(text_of(format(text, "{} {}", true, false)) == TextView{"true false"});
    assert(text_of(format(text, "[{}]", TextView{"the queue"})) == TextView{"[the queue]"});
    assert(text_of(format(text, "[{}]", TextView{})) == TextView{"[]"});
    assert(text_of(format(text, "{} holds {} of {}", TextView{"ring"}, 3, 4)) == TextView{"ring holds 3 of 4"});
}

// A brace in an argument is written as it is, and `{{` and `}}` write one
// brace each.
void test_format_writes_escapes_and_braces_of_arguments() {
    FixedText<64> text;
    assert(text_of(format(text, "{{}}")) == TextView{"{}"});
    assert(text_of(format(text, "{{{}}}", 5)) == TextView{"{5}"});
    assert(text_of(format(text, "{}", TextView{"{}"})) == TextView{"{}"});
}

// A text that fills the buffer exactly fits.  A text one character longer
// gives Truncated, and the buffer holds the first characters of the text.
void test_format_refuses_a_text_that_does_not_fit() {
    FixedText<5> text;
    assert(text_of(format(text, "{}", 12345)) == TextView{"12345"});
    auto const longer = format(text, "{}", 123456);
    assert(longer.is_err());
    assert(text.size() == 5);
    assert(text.view() == TextView{"12345"});
    auto const much_longer = format(text, "the queue holds {} items", 3);
    assert(much_longer.is_err());
    assert(text.view() == TextView{"the q"});
}

// A second format() writes from the start of the buffer.
void test_format_writes_from_the_start() {
    FixedText<16> text;
    assert(text_of(format(text, "a long text")) == TextView{"a long text"});
    assert(text_of(format(text, "{}", 1)) == TextView{"1"});
    assert(text.size() == 1);
}

// A text longer than one window of the cold formatter keeps each byte.
void test_format_keeps_a_long_text() {
    FixedText<1200> text;
    TextView const tens{"0123456789"};
    auto const result = format(text, "{}{}{}{}{}{}{}{}{}{}|{}{}{}{}{}{}{}{}{}{}", tens, tens, tens, tens, tens, tens,
                               tens, tens, tens, tens, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    assert(result.is_ok());
    assert(text.size() == 112);
}

}  // namespace

int main() {
    test_text_view_compares_its_characters();
    test_fixed_text_starts_empty();
    test_format_writes_integers_in_decimal();
    test_format_writes_bools_and_texts();
    test_format_writes_escapes_and_braces_of_arguments();
    test_format_refuses_a_text_that_does_not_fit();
    test_format_writes_from_the_start();
    test_format_keeps_a_long_text();
    crucible::test::pass("test_core_text: all tests passed\n");
    return 0;
}
