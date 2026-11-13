// Tests of the growable owners of the Region family: append across many
// growths, the refusal at the maximum, the Full of a linear element, the
// moves of elements that are not bytewise, the end of each element, an
// append of a view of the same text, the foreign doors of Region.h, and the
// CText of a text that keeps a zero after its characters.

#include <foundation/core/Grow.h>

#include <foundation/ByteSeal.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include "../test_assert.h"
#include "abort_probe.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

// A list of integers grows from empty through many doublings, and each read
// sees each appended value in order.
void test_append_across_growths() {
    ::foundation::effects::Alloc const alloc{};
    GrowList<std::uint32_t> list = mint_grow_list<std::uint32_t>(100000);
    assert(list.size() == 0 && list.view().size() == 0 && list.get(0).is_none());
    for (std::uint32_t value = 0; value < 5000; ++value) {
        list.append(alloc, value * 3).expect("the list is below its maximum");
    }
    assert(list.size() == 5000 && list.view().size() == 5000);
    std::uint32_t expected = 0;
    for (std::uint32_t const element : list) {
        assert(element == expected * 3);
        ++expected;
    }
    assert(expected == 5000);
    assert(*list.get(4999).expect("4999 is below the count") == 4999 * 3);
    assert(list.get(5000).is_none());
    *list.get(7).expect("7 is below the count") = 1;
    GrowList<std::uint32_t> const& fixed = list;
    assert(*fixed.get(7).expect("7 is below the count") == 1);

    // clear() keeps the storage, and the next appends reuse it.
    list.clear();
    assert(list.size() == 0 && list.get(0).is_none());
    list.append(alloc, 9).expect("a cleared list is below its maximum");
    assert(list.size() == 1 && *list.get(0).expect("0 is below the count") == 9);
}

// An append at the maximum gives Full and changes nothing.
void test_refusal_at_the_maximum() {
    ::foundation::effects::Alloc const alloc{};
    GrowList<int> list = mint_grow_list<int>(3);
    for (int value = 1; value <= 3; ++value) {
        list.append(alloc, value).expect("the list is below its maximum");
    }
    assert(list.append(alloc, 4).is_err());
    assert(list.size() == 3 && *list.get(2).expect("2 is below the count") == 3);

    GrowList<int> none_fit = mint_grow_list<int>(0);
    assert(none_fit.append(alloc, 1).is_err() && none_fit.size() == 0);
}

// The Full of a linear element gives the value back, and a growth moves each
// token into the new storage.
void test_full_gives_a_linear_value_back() {
    ::foundation::effects::Alloc const alloc{};
    GrowList<Token> list = mint_grow_list<Token>(40);
    for (int id = 0; id < 40; ++id) {
        list.append(alloc, Token{id}).expect("the list is below its maximum");
    }
    auto refused = list.append(alloc, Token{99});
    assert(refused.is_err());
    Token back = std::move(refused).err().expect("the list is full").take();
    assert(back.id == 99);
    ::foundation::linear::drop(std::move(back));
    int expected = 0;
    for (Token const& token : list) {
        assert(token.id == expected);
        ++expected;
    }
    assert(expected == 40);
    ::foundation::linear::drop(std::move(list));
}

// Each element that is not bytewise moves at each growth and ends exactly one
// time: at a clear or at the end of the list.
void test_each_element_ends_once() {
    int ends = 0;
    auto const counter = ::foundation::core::to(ends);
    using CounterRef = std::remove_const_t<decltype(counter)>;
    using Element = Counted<CounterRef>;
    ::foundation::effects::Alloc const alloc{};
    {
        GrowList<Element> list = mint_grow_list<Element>(1000);
        for (int label = 0; label < 300; ++label) {
            list.append(alloc, Element{counter, label}).expect("the list is below its maximum");
        }
        assert(ends == 0 && list.size() == 300);
        int expected = 0;
        for (Element const& element : list) {
            assert(element.is_live && element.label == expected);
            ++expected;
        }
        GrowList<Element> moved{std::move(list)};
        assert(moved.size() == 300 && ends == 0);
        moved.clear();
        assert(ends == 300);
        for (int label = 0; label < 5; ++label) {
            moved.append(alloc, Element{counter, label}).expect("the list is below its maximum");
        }
    }
    assert(ends == 305);
}

// A list of lists grows, and each inner list keeps its elements.
void test_nested_lists() {
    ::foundation::effects::Alloc const alloc{};
    GrowList<GrowList<int>> pools = mint_grow_list<GrowList<int>>(64);
    for (int pool = 0; pool < 20; ++pool) {
        GrowList<int> members = mint_grow_list<int>(64);
        for (int member = 0; member <= pool; ++member) {
            members.append(alloc, pool * 100 + member).expect("the pool is below its maximum");
        }
        pools.append(alloc, std::move(members)).expect("the pools are below their maximum");
    }
    int pool = 0;
    for (GrowList<int> const& members : pools) {
        assert(members.size() == static_cast<std::size_t>(pool) + 1);
        assert(*members.get(0).expect("each pool holds a member") == pool * 100);
        ++pool;
    }
    assert(pool == 20);
}

// A text grows through many appends of each kind, and refuses an append that
// does not fit below its maximum.
void test_text_appends() {
    ::foundation::effects::Alloc const alloc{};
    GrowText text = mint_grow_text(4096);
    assert(text.size() == 0 && text.view() == TextView{});
    text.append(alloc, "crucible").expect("the text is below its maximum");
    text.append(alloc, '/').expect("the text is below its maximum");
    FixedArray<char, 3> const letters{'a', 'b', 'c'};
    text.append(alloc, letters.view()).expect("the text is below its maximum");
    assert(text.view() == TextView{"crucible/abc"});
    for (int turn = 0; turn < 200; ++turn) {
        text.append(alloc, "0123456789").expect("the text is below its maximum");
    }
    assert(text.size() == 12 + 2000);
    std::size_t digits = 0;
    for (char const character : text) {
        if (character >= '0' && character <= '9') {
            ++digits;
        }
    }
    assert(digits == 2000);

    GrowText small = mint_grow_text(5);
    assert(small.append(alloc, "abcdef").is_err() && small.size() == 0);
    small.append(alloc, "abc").expect("three characters fit below five");
    assert(small.append(alloc, "abc").is_err() && small.view() == TextView{"abc"});
    small.append(alloc, "de").expect("two characters fit below five");
    assert(small.view() == TextView{"abcde"} && small.append(alloc, 'f').is_err());
    small.clear();
    assert(small.size() == 0);
    small.append(alloc, "xyz").expect("a cleared text is below its maximum");
    assert(small.view() == TextView{"xyz"});
}

// An append of a view of the same text copies the characters before the
// growth gives the old storage back.
void test_text_appends_itself() {
    ::foundation::effects::Alloc const alloc{};
    GrowText text = mint_grow_text(1 << 20);
    text.append(alloc, "ab").expect("the text is below its maximum");
    for (int turn = 0; turn < 12; ++turn) {
        text.append(alloc, text.view()).expect("the text is below its maximum");
    }
    assert(text.size() == std::size_t{2} << 12);
    std::size_t position = 0;
    for (char const character : text) {
        assert(character == (position % 2 == 0 ? 'a' : 'b'));
        ++position;
    }
    GrowText moved = std::move(text);
    assert(moved.size() == std::size_t{2} << 12);
    GrowText other = mint_grow_text(8);
    other.append(alloc, "q").expect("the text is below its maximum");
    other = std::move(moved);
    assert(other.size() == std::size_t{2} << 12);
}

// The property: random appends to a list and a text agree with a model of a
// count and a sum, and a refusal comes exactly at the maximum.
void test_agrees_with_a_model() {
    ::foundation::effects::Alloc const alloc{};
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
            assert(is_refused == (model_count == maximum) && is_text_refused == is_refused);
            if (!is_refused) {
                model_sum += value;
                ++model_count;
            }
        }
        std::uint64_t sum = 0;
        for (std::uint64_t const element : list) {
            sum += element;
        }
        assert(sum == model_sum && list.size() == model_count && text.size() == model_count);
    }
}

// The foreign doors give a View and a TextView of a run that a caller holds,
// and refuse a null pointer with a count.
void test_foreign_doors() {
    FixedArray<float, 4> const numbers{1.0f, 2.0f, 3.0f, 4.0f};
    View<float const> const view = ::foundation::core::mint_foreign_view(&*numbers.get(0).expect("4 elements"), 4);
    assert(view.size() == 4 && *view.get(3).expect("3 is below the count") > 3.5f);
    FixedArray<char, 2> const letters{'o', 'k'};
    TextView const text = ::foundation::core::mint_foreign_text(&*letters.get(0).expect("2 elements"), 2);
    assert(text == TextView{"ok"});
    assert(::foundation::core::mint_foreign_view(static_cast<float const*>(nullptr), 0).size() == 0);
    assert(::crucible::test::aborts(
        [] { static_cast<void>(::foundation::core::mint_foreign_view(static_cast<float const*>(nullptr), 1)); }));
    assert(::crucible::test::aborts(
        [] { static_cast<void>(::foundation::core::mint_foreign_text(static_cast<char const*>(nullptr), 1)); }));
}

// mint_c_text gives the CText of a view whose last character is its only
// zero, and no value for each other view.  foreign_c_str gives the address
// of the first character, and the zero follows the last character.
void test_c_text_doors() {
    ::foundation::effects::Alloc const alloc{};
    GrowText name = mint_grow_text(64);
    name.append(alloc, "aten::mm").expect("the text is below its maximum");
    // A view with no zero at its end.
    assert(::foundation::core::mint_c_text(name.view()).is_none());
    name.append(alloc, '\0').expect("the text is below its maximum");
    // A valid name: the zero is the last character of the view.
    CText const c_text = ::foundation::core::mint_c_text(name.view()).expect("one zero ends the view");
    assert(c_text.size() == 8 && c_text.view() == TextView{"aten::mm"});
    assert(::foundation::core::foreign_c_str(c_text)[0] == 'a');
    assert(::foundation::core::foreign_c_str(c_text)[7] == 'm');
    assert(::foundation::core::foreign_c_str(c_text)[8] == '\0');
    // The zero after a view is no part of it, so the view of the characters
    // before the zero gives no value.
    TextView const before_zero = name.view().window(0, 8).expect("8 characters fit");
    assert(::foundation::core::mint_c_text(before_zero).is_none());
    // An empty view.
    assert(::foundation::core::mint_c_text(TextView{}).is_none());
    // A view with a zero inside.
    GrowText inner = mint_grow_text(64);
    inner.append(alloc, "aten").expect("the text is below its maximum");
    inner.append(alloc, '\0').expect("the text is below its maximum");
    inner.append(alloc, "mm").expect("the text is below its maximum");
    inner.append(alloc, '\0').expect("the text is below its maximum");
    assert(::foundation::core::mint_c_text(inner.view()).is_none());
    // A view of one zero gives the empty CText.
    GrowText zero = mint_grow_text(4);
    zero.append(alloc, '\0').expect("the text is below its maximum");
    assert(::foundation::core::mint_c_text(zero.view()).expect("one zero ends the view").size() == 0);
    // The CText of a literal.
    assert(::foundation::core::foreign_c_str(CText{"ok"})[2] == '\0');
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
    test_agrees_with_a_model();
    test_foreign_doors();
    test_c_text_doors();
    return 0;
}
