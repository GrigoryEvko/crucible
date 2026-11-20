// Tests of the growable owners of the Region family: append across many
// growths, the refusal at the maximum, the Full of a linear element, the
// moves of elements that are not bytewise, the end of each element, an
// append of a part of the same text, the frame doors, the freeze into a
// HeapText, the foreign doors of Region.h, and the CText of a text that keeps
// a zero after its characters.  test/fixy/test_core_grow_lend.cpp holds the
// deaths of THE LEND COUNT.

#include <foundation/core/Grow.h>

#include <foundation/ByteSeal.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::CText;
using ::foundation::core::FixedArray;
using ::foundation::core::GrowList;
using ::foundation::core::GrowText;
using ::foundation::core::mint_grow_list;
using ::foundation::core::mint_grow_text;
using ::foundation::core::TextView;
using ::foundation::core::View;

// An element that counts its ends through a Ref to a counter.  A moved-from
// element is not live, so only a live element counts.
template <class CounterRef>
struct Counted final {
    CounterRef ends;
    int label = 0;
    bool is_live = true;
    Counted(CounterRef counter, int from) noexcept : ends{counter}, label{from} {}
    Counted(Counted&& other) noexcept : ends{other.ends}, label{other.label} { other.is_live = false; }
    Counted(Counted const&) = delete;
    Counted& operator=(Counted&&) = delete;
    Counted& operator=(Counted const&) = delete;
    ~Counted() {
        if (is_live) {
            ++*ends;
        }
    }
};

// A linear element.  The seal makes its assignment user-provided, so it is
// not trivially copyable, and it refuses a lifetime start over bytes.
struct[[= ::foundation::linear::linear{}]] Token final {
    int id = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    explicit Token(int from) noexcept : id{from} {}
    Token(Token&& other) noexcept : id{other.id} {}
    Token& operator=(Token&& other) noexcept {
        id = other.id;
        return *this;
    }
    Token(Token const&) = delete;
};

// The owner that a call gives back by reference.  The rule use-after-move
// does not read through it, so a test reads a frozen text again.
template <class Owner>
[[nodiscard]] Owner& through(Owner& owner) noexcept {
    return owner;
}

// The element at position of a list, or no value at or past its size,
// through the frame door with_element.
template <class T>
[[nodiscard]] ::foundation::core::Option<T> element_at(GrowList<T> const& list, std::size_t position) noexcept {
    return list.with_element(
        position, [](T const& element) noexcept { return ::foundation::core::Option<T>::some(element); },
        [] noexcept { return ::foundation::core::Option<T>{::foundation::core::none}; });
}

// The count of the elements of a list, through the frame door with_view.
template <class T>
[[nodiscard]] std::size_t count_of(GrowList<T> const& list) noexcept {
    return list.with_view([](View<T const> view) noexcept { return view.size(); });
}

// True when a text holds the characters of expected, through the frame door
// with_view.
[[nodiscard]] bool holds(GrowText const& text, TextView expected) noexcept {
    return text.with_view([expected](TextView characters) noexcept { return characters == expected; });
}

// A list of integers grows from empty through many doublings, and each read
// sees each appended value in order.
void test_append_across_growths() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowList<std::uint32_t> list = mint_grow_list<std::uint32_t>(100000);
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 0 && count_of(list) == 0 && element_at(list, 0).is_none());
    for (std::uint32_t value = 0; value < 5000; ++value) {
        list.append(alloc, value * 3).expect("the list is below its maximum");
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 5000 && count_of(list) == 5000);
    std::uint32_t const in_order = list.with_view([](View<std::uint32_t const> view) noexcept {
        std::uint32_t expected = 0;
        for (std::uint32_t const element : view) {
            if (element != expected * 3) break;
            ++expected;
        }
        return expected;
    });
    CRUCIBLE_TEST_ASSERT(kTest, in_order == 5000);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(list, 4999).expect("4999 is below the count") == 4999 * 3);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(list, 5000).is_none());
    bool const is_written = list.with_element(
        7,
        [](std::uint32_t& element) noexcept {
            element = 1;
            return true;
        },
        [] noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written && element_at(list, 7).expect("7 is below the count") == 1);

    // clear() keeps the storage, and the next appends reuse it.
    list.clear();
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 0 && element_at(list, 0).is_none());
    list.append(alloc, 9).expect("a cleared list is below its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 1 && element_at(list, 0).expect("0 is below the count") == 9);
}

// An append at the maximum gives Full and changes nothing.
void test_refusal_at_the_maximum() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowList<int> list = mint_grow_list<int>(3);
    for (int value = 1; value <= 3; ++value) {
        list.append(alloc, value).expect("the list is below its maximum");
    }
    CRUCIBLE_TEST_ASSERT(kTest, list.append(alloc, 4).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 3 && element_at(list, 2).expect("2 is below the count") == 3);

    GrowList<int> none_fit = mint_grow_list<int>(0);
    CRUCIBLE_TEST_ASSERT(kTest, none_fit.append(alloc, 1).is_err() && none_fit.size() == 0);
}

// The Full of a linear element gives the value back, and a growth moves each
// token into the new storage.
void test_full_gives_a_linear_value_back() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowList<Token> list = mint_grow_list<Token>(40);
    for (int id = 0; id < 40; ++id) {
        list.append(alloc, Token{id}).expect("the list is below its maximum");
    }
    auto refused = list.append(alloc, Token{99});
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err());
    Token back = std::move(refused).err().expect("the list is full").take();
    CRUCIBLE_TEST_ASSERT(kTest, back.id == 99);
    ::foundation::linear::drop(std::move(back));
    int const in_order = list.with_view([](View<Token const> view) noexcept {
        int expected = 0;
        for (Token const& token : view) {
            if (token.id != expected) break;
            ++expected;
        }
        return expected;
    });
    CRUCIBLE_TEST_ASSERT(kTest, in_order == 40);
    ::foundation::linear::drop(std::move(list));
}

// Each element that is not bytewise moves at each growth and ends exactly one
// time: at a clear or at the end of the list.
void test_each_element_ends_once() {
    int ends = 0;
    auto const counter = ::foundation::core::to(ends);
    using CounterRef = std::remove_const_t<decltype(counter)>;
    using Element = Counted<CounterRef>;
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        GrowList<Element> list = mint_grow_list<Element>(1000);
        for (int label = 0; label < 300; ++label) {
            list.append(alloc, Element{counter, label}).expect("the list is below its maximum");
        }
        CRUCIBLE_TEST_ASSERT(kTest, ends == 0 && list.size() == 300);
        int const in_order = list.with_view([](View<Element const> view) noexcept {
            int expected = 0;
            for (Element const& element : view) {
                if (!element.is_live || element.label != expected) break;
                ++expected;
            }
            return expected;
        });
        CRUCIBLE_TEST_ASSERT(kTest, in_order == 300);
        GrowList<Element> moved{std::move(list)};
        CRUCIBLE_TEST_ASSERT(kTest, moved.size() == 300 && ends == 0);
        moved.clear();
        CRUCIBLE_TEST_ASSERT(kTest, ends == 300);
        for (int label = 0; label < 5; ++label) {
            moved.append(alloc, Element{counter, label}).expect("the list is below its maximum");
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends == 305);
}

// A list of lists grows, and each inner list keeps its elements.
void test_nested_lists() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowList<GrowList<int>> pools = mint_grow_list<GrowList<int>>(64);
    for (int pool = 0; pool < 20; ++pool) {
        GrowList<int> members = mint_grow_list<int>(64);
        for (int member = 0; member <= pool; ++member) {
            members.append(alloc, pool * 100 + member).expect("the pool is below its maximum");
        }
        pools.append(alloc, std::move(members)).expect("the pools are below their maximum");
    }
    int const in_order = pools.with_view([](View<GrowList<int> const> view) noexcept {
        int pool = 0;
        for (GrowList<int> const& members : view) {
            bool const holds_pool = members.size() == static_cast<std::size_t>(pool) + 1
                                 && element_at(members, 0).expect("each pool holds a member") == pool * 100;
            if (!holds_pool) break;
            ++pool;
        }
        return pool;
    });
    CRUCIBLE_TEST_ASSERT(kTest, in_order == 20);
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

// A frame door gives its body the elements or the characters, and gives the
// result of the body.  with_element writes through its arm, and gives on_none
// at and past size().  A const owner lends const elements.  After a frame,
// the owner appends again.
void test_frame_doors() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    GrowList<std::uint32_t> list = mint_grow_list<std::uint32_t>(1000);
    for (std::uint32_t value = 0; value < 300; ++value) {
        list.append(alloc, value).expect("the list is below its maximum");
    }
    std::uint64_t const sum = list.with_view([](View<std::uint32_t const> view) noexcept {
        std::uint64_t total = 0;
        for (std::uint32_t const element : view) {
            total += element;
        }
        return total;
    });
    CRUCIBLE_TEST_ASSERT(kTest, sum == std::uint64_t{299} * 300 / 2);
    bool const is_written = list.with_element(
        7,
        [](std::uint32_t& element) noexcept {
            element = 70;
            return true;
        },
        [] noexcept { return false; });
    bool const is_past_end =
        list.with_element(300, [](std::uint32_t&) noexcept { return false; }, [] noexcept { return true; });
    CRUCIBLE_TEST_ASSERT(kTest, is_written && is_past_end);
    GrowList<std::uint32_t> const& fixed = list;
    std::uint32_t const seventh = fixed.with_element(
        7, [](std::uint32_t const& element) noexcept { return element; }, [] noexcept { return std::uint32_t{0}; });
    std::size_t const count = fixed.with_view([](View<std::uint32_t const> view) noexcept { return view.size(); });
    CRUCIBLE_TEST_ASSERT(kTest, seventh == 70 && count == 300);
    list.append(alloc, 300).expect("the list is below its maximum");
    CRUCIBLE_TEST_ASSERT(kTest, list.size() == 301);

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

// The property: random appends to a list and a text agree with a model of a
// count and a sum, and a refusal comes exactly at the maximum.
void test_agrees_with_a_model() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::crucible::test::PhiloxStream stream{0x5EEDA77A00000002u};
    for (int trial = 0; trial < 64; ++trial) {
        std::size_t const maximum = stream.below(500);
        GrowList<std::uint64_t> list = mint_grow_list<std::uint64_t>(maximum);
        GrowText text = mint_grow_text(maximum);
        std::size_t const appends = stream.below(600);
        std::uint64_t model_sum = 0;
        std::size_t model_count = 0;
        for (std::size_t turn = 0; turn < appends; ++turn) {
            std::uint64_t const value = stream.next_wide();
            bool const is_refused = list.append(alloc, value).is_err();
            bool const is_text_refused = text.append(alloc, 'x').is_err();
            CRUCIBLE_TEST_ASSERT(kTest, is_refused == (model_count == maximum) && is_text_refused == is_refused);
            if (!is_refused) {
                model_sum += value;
                ++model_count;
            }
        }
        std::uint64_t const sum = list.with_view([](View<std::uint64_t const> view) noexcept {
            std::uint64_t total = 0;
            for (std::uint64_t const element : view) {
                total += element;
            }
            return total;
        });
        CRUCIBLE_TEST_ASSERT(kTest, sum == model_sum && list.size() == model_count && text.size() == model_count);
    }
}

// The foreign doors give a View and a TextView of a run that a caller holds,
// and refuse a null pointer with a count.
void test_foreign_doors() {
    FixedArray<float, 4> const numbers{1.0f, 2.0f, 3.0f, 4.0f};
    View<float const> const view = ::foundation::core::mint_foreign_view(&*numbers.get(0).expect("4 elements"), 4);
    CRUCIBLE_TEST_ASSERT(kTest, view.size() == 4 && *view.get(3).expect("3 is below the count") > 3.5f);
    FixedArray<char, 2> const letters{'o', 'k'};
    TextView const text = ::foundation::core::mint_foreign_text(&*letters.get(0).expect("2 elements"), 2);
    CRUCIBLE_TEST_ASSERT(kTest, text == TextView{"ok"});
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
        ::fixy::TextView{"foundation: fatal: view_over_ got a null pointer with a count"});
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
    test_append_across_growths();
    test_refusal_at_the_maximum();
    test_full_gives_a_linear_value_back();
    test_each_element_ends_once();
    test_nested_lists();
    test_text_appends();
    test_text_appends_itself();
    test_frame_doors();
    test_freeze();
    test_agrees_with_a_model();
    test_foreign_doors();
    test_c_text_doors();
    return 0;
}
