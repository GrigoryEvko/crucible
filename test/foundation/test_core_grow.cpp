// Tests of the growable text of the Region family: append across many growths,
// the refusal at the maximum, an append of a part of the same text, the frame
// door, the freeze into a HeapText, the foreign doors of Region.h, and the CText
// of a text that keeps a zero after its characters.
// test/fixy/test_core_grow_lend.cpp holds the deaths of the lend count.

#include <foundation/core/Grow.h>

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::CText;
using ::foundation::core::FixedArray;
using ::foundation::core::GrowText;
using ::foundation::core::mint_grow_text;
using ::foundation::core::TextView;
using ::foundation::core::View;

// The owner that a call gives back by reference.  The rule use-after-move
// does not read through it, so a test reads a frozen text again.
template <class Owner>
[[nodiscard]] Owner& through(Owner& owner) noexcept {
    return owner;
}

// True when a text holds the characters of expected, through the frame door
// with_view.
[[nodiscard]] bool holds(GrowText const& text, TextView expected) noexcept {
    return text.with_view([expected](TextView characters) noexcept { return characters == expected; });
}

// A text grows through many appends of each kind, and refuses an append that
// does not fit below its maximum.
void test_text_appends() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowText text = mint_grow_text(4096);
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 0 && holds(text, TextView{}));
    text.append(alloc, "crucible").expect("the text is below its maximum");
    text.append(alloc, '/').expect("the text is below its maximum");
    FixedArray<char, 3> const letters{'a', 'b', 'c'};
    text.append(alloc, letters.view()).expect("the text is below its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, holds(text, TextView{"crucible/abc"}));
    for (int turn = 0; turn < 200; ++turn) {
        text.append(alloc, "0123456789").expect("the text is below its maximum");
    }
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 12 + 2000);
    std::size_t const digits = text.with_view([](TextView characters) noexcept {
        std::size_t found = 0;
        for (char const character : characters) {
            found += character >= '0' && character <= '9' ? 1 : 0;
        }
        return found;
    });
    CRUCIBLE_TEST_ASSERT(kTest, digits == 2000);

    GrowText small = mint_grow_text(5);
    CRUCIBLE_TEST_ASSERT(kTest, small.append(alloc, "abcdef").is_err() && small.size() == 0);
    small.append(alloc, "abc").expect("three characters fit below five");
    CRUCIBLE_TEST_ASSERT(kTest, small.append(alloc, "abc").is_err() && holds(small, TextView{"abc"}));
    small.append(alloc, "de").expect("two characters fit below five");
    CRUCIBLE_TEST_ASSERT(kTest, holds(small, TextView{"abcde"}) && small.append(alloc, 'f').is_err());
    small.clear();
    CRUCIBLE_TEST_ASSERT(kTest, small.size() == 0);
    small.append(alloc, "xyz").expect("a cleared text is below its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, holds(small, TextView{"xyz"}));
}

// append_self copies the characters of the text before the growth gives the
// old storage back.  Each doubling of the text grows its storage, so each
// turn after the first storage is full reads the old storage in the growth.
// A window of the text appends a part of it.
void test_text_appends_itself() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowText text = mint_grow_text(1 << 20);
    text.append(alloc, "ab").expect("the text is below its maximum");
    for (int turn = 0; turn < 12; ++turn) {
        text.append_self(alloc, 0, text.size()).expect("the text is below its maximum");
    }
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == std::size_t{2} << 12);
    GrowText window = mint_grow_text(64);
    window.append(alloc, "crucible").expect("the text is below its maximum");
    window.append_self(alloc, 2, 3).expect("the text is below its maximum");
    window.append_self(alloc, 0, 0).expect("an empty window fits");
    CRUCIBLE_TEST_ASSERT(kTest, holds(window, TextView{"crucibleuci"}));
    GrowText full = mint_grow_text(4);
    full.append(alloc, "abc").expect("the text is below its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, full.append_self(alloc, 0, 2).is_err() && holds(full, TextView{"abc"}));
    std::size_t const in_order = text.with_view([](TextView characters) noexcept {
        std::size_t position = 0;
        for (char const character : characters) {
            if (character != (position % 2 == 0 ? 'a' : 'b')) break;
            ++position;
        }
        return position;
    });
    CRUCIBLE_TEST_ASSERT(kTest, in_order == std::size_t{2} << 12);
    GrowText moved = std::move(text);
    CRUCIBLE_TEST_ASSERT(kTest, moved.size() == std::size_t{2} << 12);
    GrowText other = mint_grow_text(8);
    other.append(alloc, "q").expect("the text is below its maximum");
    other = std::move(moved);
    CRUCIBLE_TEST_ASSERT(kTest, other.size() == std::size_t{2} << 12);
}

// The frame door gives its body the characters, and gives the result of the
// body.  After a frame, the text appends again.
void test_frame_door() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowText text = mint_grow_text(64);
    text.append(alloc, "aten::mm").expect("the text is below its maximum");
    std::size_t const colons = text.with_view([](TextView characters) noexcept {
        std::size_t found = 0;
        for (char const character : characters) {
            found += character == ':' ? 1 : 0;
        }
        return found;
    });
    CRUCIBLE_TEST_ASSERT(kTest, colons == 2);
    text.append(alloc, '\0').expect("the text is below its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 9);
}

// A freeze gives the characters to a HeapText with no copy, and leaves the
// text empty with its maximum.  The HeapText keeps its characters across each
// later append of the text, and across a move.
void test_freeze() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowText text = mint_grow_text(8);
    text.append(alloc, "crucible").expect("the text is below its maximum");
    ::foundation::core::HeapText const frozen = freeze(std::move(through(text)));
    CRUCIBLE_TEST_ASSERT(kTest, frozen.size() == 8 && frozen.view() == TextView{"crucible"});
    CRUCIBLE_TEST_ASSERT(kTest, text.size() == 0 && holds(text, TextView{}));
    text.append(alloc, "12345678").expect("a frozen text keeps its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, text.append(alloc, '9').is_err());
    std::size_t letters = 0;
    for (char const character : frozen) {
        letters += character >= 'a' && character <= 'z' ? 1 : 0;
    }
    CRUCIBLE_TEST_ASSERT(kTest, letters == 8 && frozen.view() == TextView{"crucible"});
    ::foundation::core::HeapText moved = freeze(std::move(through(text)));
    CRUCIBLE_TEST_ASSERT(kTest, moved.view() == TextView{"12345678"});
    moved = freeze(mint_grow_text(4));
    CRUCIBLE_TEST_ASSERT(kTest, moved.size() == 0);
}

// The property: random appends to a text agree with a model of its count, and a
// refusal comes exactly at the maximum.
void test_agrees_with_a_model() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::crucible::test::PhiloxStream stream{0x5EEDA77A00000002u};
    for (int trial = 0; trial < 64; ++trial) {
        std::size_t const maximum = stream.below(500);
        GrowText text = mint_grow_text(maximum);
        std::size_t const appends = stream.below(600);
        std::size_t model_count = 0;
        for (std::size_t turn = 0; turn < appends; ++turn) {
            bool const is_refused = text.append(alloc, 'x').is_err();
            CRUCIBLE_TEST_ASSERT(kTest, is_refused == (model_count == maximum));
            if (!is_refused) {
                ++model_count;
            }
        }
        std::size_t const written = text.with_view([](TextView characters) noexcept {
            std::size_t found = 0;
            for (char const character : characters) {
                found += character == 'x' ? 1 : 0;
            }
            return found;
        });
        CRUCIBLE_TEST_ASSERT(kTest, text.size() == model_count && written == model_count);
    }
}

// The foreign doors give a View and a TextView of a run that a caller holds,
// and refuse a null pointer with a count.
void test_foreign_doors() {
    FixedArray<float, 4> const numbers{1.0f, 2.0f, 3.0f, 4.0f};
    View<float const> const view = ::foundation::core::mint_foreign_view(&*numbers.get(0).expect("4 elements"), 4);
    CRUCIBLE_TEST_ASSERT(kTest, view.size() == 4 && *view.get(3).expect("3 is below the count") > 3.5f);
    FixedArray<char, 2> const letters{'o', 'k'};
    TextView const text = ::foundation::core::mint_foreign_text(&*letters.get(0).expect("2 elements"), 2).characters();
    CRUCIBLE_TEST_ASSERT(kTest, text == TextView{"ok"});
    // The bytes of a View give one character for each byte, also a zero byte
    // and a byte past 127.
    FixedArray<std::byte, 3> const bytes{std::byte{'a'}, std::byte{0}, std::byte{0xFF}};
    TextView const of_bytes = ::foundation::core::mint_foreign_text(bytes.view()).characters();
    CRUCIBLE_TEST_ASSERT(kTest, of_bytes.size() == 3 && of_bytes.window(0, 1).expect("3 characters") == TextView{"a"});
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::find(of_bytes, '\0') == ::foundation::core::some(std::size_t{1}));
    CRUCIBLE_TEST_ASSERT(kTest,
                         ::foundation::core::mint_foreign_text(View<std::byte const>{}).characters().size() == 0);
    CRUCIBLE_TEST_ASSERT(kTest,
                         ::foundation::core::mint_foreign_view(static_cast<float const*>(nullptr), 0).size() == 0);
    // A null pointer with a count ends the process.  Each call runs in a
    // child through expect_dies of the Check family.
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            static_cast<void>(::foundation::core::mint_foreign_view(static_cast<float const*>(nullptr), 1));
        },
        ::fixy::TextView{"foundation: fatal: mint_foreign_view got a null pointer with a count"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept { static_cast<void>(::foundation::core::mint_foreign_text(static_cast<char const*>(nullptr), 1)); },
        ::fixy::TextView{"foundation: fatal: mint_foreign_text got a null pointer with a count"});
}

// mint_c_text gives the CText of a view whose last character is its only
// zero, and no value for each other view.  foreign_c_str gives the address
// of the first character, and the zero follows the last character.
void test_c_text_doors() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowText growing = mint_grow_text(64);
    growing.append(alloc, "aten::mm").expect("the text is below its maximum");
    // A view with no zero at its end.
    CRUCIBLE_TEST_ASSERT(kTest, growing.with_view([](TextView characters) noexcept {
        return ::foundation::core::mint_c_text(characters).is_none();
    }));
    growing.append(alloc, '\0').expect("the text is below its maximum");
    // A valid name: the zero is the last character of the view.  The name is
    // frozen, so its CText lives as long as the HeapText.
    ::foundation::core::HeapText const name = freeze(std::move(growing));
    CText const c_text = ::foundation::core::mint_c_text(name.view()).expect("one zero ends the view");
    CRUCIBLE_TEST_ASSERT(kTest, c_text.size() == 8 && c_text.view() == TextView{"aten::mm"});
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::foreign_c_str(c_text)[0] == 'a');
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::foreign_c_str(c_text)[7] == 'm');
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::foreign_c_str(c_text)[8] == '\0');
    // The zero after a view is no part of it, so the view of the characters
    // before the zero gives no value.
    TextView const before_zero = name.view().window(0, 8).expect("8 characters fit");
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::mint_c_text(before_zero).is_none());
    // An empty view.
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::mint_c_text(TextView{}).is_none());
    // A view with a zero inside.
    GrowText inner = mint_grow_text(64);
    inner.append(alloc, "aten").expect("the text is below its maximum");
    inner.append(alloc, '\0').expect("the text is below its maximum");
    inner.append(alloc, "mm").expect("the text is below its maximum");
    inner.append(alloc, '\0').expect("the text is below its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, inner.with_view([](TextView characters) noexcept {
        return ::foundation::core::mint_c_text(characters).is_none();
    }));
    // A view of one zero gives the empty CText.
    GrowText zero = mint_grow_text(4);
    zero.append(alloc, '\0').expect("the text is below its maximum");
    std::size_t const empty_size = zero.with_view([](TextView characters) noexcept {
        return ::foundation::core::mint_c_text(characters).expect("one zero ends the view").size();
    });
    CRUCIBLE_TEST_ASSERT(kTest, empty_size == 0);
    // The CText of a literal.
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::foreign_c_str(CText{"ok"})[2] == '\0');
}

}  // namespace

int main() {
    test_text_appends();
    test_text_appends_itself();
    test_frame_door();
    test_freeze();
    test_agrees_with_a_model();
    test_foreign_doors();
    test_c_text_doors();
    return 0;
}
