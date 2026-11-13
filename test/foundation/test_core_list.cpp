// Tests of the FixedList of the Region family: push, take_last, clear, get,
// the copy and the move, the end of each element, and the Full of a linear
// element.  The property compares a list with a model: a FixedArray and a
// count.

#include <foundation/core/List.h>

#include <foundation/ByteSeal.h>
#include <foundation/core/Ref.h>

#include "../test_assert.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::FixedList;
using ::foundation::core::Index;
using ::foundation::core::View;

// An element that counts its ends through a Ref to a counter.  The brand of
// the Ref is a template argument, so no fresh brand leaves its scope.  A
// moved-from element is not live, so only a live element counts.
template <class CounterRef>
struct Counted final {
    CounterRef ends;
    bool is_live = true;
    explicit Counted(CounterRef counter) noexcept : ends{counter} {}
    Counted(Counted&& other) noexcept : ends{other.ends} { other.is_live = false; }
    Counted(Counted const& other) noexcept : ends{other.ends}, is_live{other.is_live} {}
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

void test_push_until_full() {
    FixedList<int, 4> list;
    for (int value = 1; value <= 4; ++value) {
        int& element = *list.push(value).expect("the list has room");
        assert(element == value);
        element *= 10;
    }
    assert(list.size() == 4);
    assert(list.push(5).is_err());
    assert(list.size() == 4);
    int expected = 10;
    for (int const element : list) {
        assert(element == expected);
        expected += 10;
    }
    assert(list.get(3).is_some() && list.get(4).is_none());
}

void test_take_last_and_clear() {
    FixedList<int, 3> list;
    assert(list.take_last().is_none());
    static_cast<void>(list.push(1).expect("the list has room"));
    static_cast<void>(list.push(2).expect("the list has room"));
    static_cast<void>(list.push(3).expect("the list has room"));
    assert(list.take_last().expect("the list holds three elements") == 3);
    assert(list.take_last().expect("the list holds two elements") == 2);
    assert(list.size() == 1);
    list.clear();
    assert(list.size() == 0 && list.view().size() == 0 && list.take_last().is_none());
    static_cast<void>(list.push(7).expect("a cleared list has room"));
    assert(*list.get(0).expect("0 is below the count") == 7);
}

// Each element ends exactly one time: at a take, at a clear, or at the end of
// the list.  A copy of the list copies each element.
void test_each_element_ends_once() {
    int ends = 0;
    auto const counter = ::foundation::core::to(ends);
    using CounterRef = std::remove_const_t<decltype(counter)>;
    using Element = Counted<CounterRef>;
    {
        FixedList<Element, 4> list;
        for (int turn = 0; turn < 3; ++turn) {
            static_cast<void>(list.push(Element{counter}).expect("the list has room"));
        }
        assert(ends == 0);
        {
            Element const last = list.take_last().expect("the list holds three elements");
            assert(last.is_live && ::foundation::core::same_object(last.ends, counter));
        }
        assert(ends == 1);
        FixedList<Element, 4> const copied{list};
        assert(copied.size() == 2 && ends == 1);
        FixedList<Element, 4> moved{std::move(list)};
        assert(moved.size() == 2 && ends == 1);
        moved.clear();
        assert(ends == 3);
    }
    assert(ends == 5);
}

// A push into a full list of a linear element gives the value back in its
// Full, and the caller consumes each value.
void test_full_gives_a_linear_value_back() {
    FixedList<Token, 1> list;
    static_cast<void>(list.push(Token{1}).expect("the list has room"));
    auto refused = list.push(Token{2});
    assert(refused.is_err());
    Token back = std::move(refused).err().expect("the list is full").take();
    assert(back.id == 2);
    ::foundation::linear::drop(std::move(back));
    Token first = list.take_last().expect("the list holds the first token");
    assert(first.id == 1);
    ::foundation::linear::drop(std::move(first));
    assert(list.size() == 0);
    ::foundation::linear::drop(std::move(list));
}

// The property: for random pushes and takes, the list holds the elements of
// the model, in order, and refuses a push exactly when the model is full.
void test_list_agrees_with_the_model() {
    constexpr std::size_t capacity = 16;
    ::crucible::test::PhiloxStream stream{0x0F1E2D3C4B5A6978u};
    FixedList<std::uint64_t, capacity> list;
    FixedArray<std::uint64_t, capacity> model{};
    std::size_t count = 0;
    for (int step = 0; step < 4096; ++step) {
        if (stream.below(3) != 0) {
            std::uint64_t const value = stream.next_wide();
            bool const is_pushed = list.push(value).is_ok();
            assert(is_pushed == (count < capacity));
            if (is_pushed) {
                model.at(Index<capacity>::check(count).expect("the count is below the capacity")) = value;
                ++count;
            }
        } else {
            bool const is_taken = list.take_last().is_some();
            assert(is_taken == (count != 0));
            if (is_taken) {
                --count;
            }
        }
        View<std::uint64_t const> const expected = model.view().window(0, count).expect("the count fits");
        assert(list.size() == count);
        assert(::foundation::core::equal(list.view(), expected));
    }
}

void test_iteration_verbs_of_a_list() {
    FixedList<int, 5> list;
    for (int value = 1; value <= 5; ++value) {
        static_cast<void>(list.push(value).expect("the list has room"));
    }
    int expected = 5;
    for (int const element : list.reverse()) {
        assert(element == expected);
        --expected;
    }
    int windows = 0;
    for (View<int, 2> const run : list.windows<2>()) {
        assert(run.at(::foundation::core::index<1>()) == run.at(::foundation::core::index<0>()) + 1);
        ++windows;
    }
    assert(windows == 4);
    assert(list.chunks<2>().rest().size() == 1);
}

// A list of the first count numbers from 1, for a constant evaluation.
constexpr FixedList<int, 4> list_of(int count) noexcept {
    FixedList<int, 4> list;
    for (int value = 1; value <= count; ++value) {
        static_cast<void>(list.push(value).expect("the list has room"));
    }
    return list;
}

constexpr int sum_of(FixedList<int, 4> const& list) noexcept {
    int sum = 0;
    for (int const element : list) {
        sum += element;
    }
    return sum;
}

// The View of a FixedList comes from a reference to the array of its slots,
// with no compare with a null pointer.  The build sets
// -fno-delete-null-pointer-checks, and with it GCC 16 does not fold the
// compare of the address of a block-scope static object with a null pointer
// in a constant evaluation.  A loop over a static FixedList is then a
// constant expression only when the View has no such compare.
void test_view_of_a_static_list_is_constant() {
    static constexpr FixedList<int, 4> three = list_of(3);
    static constexpr FixedList<int, 4> none = list_of(0);
    static constexpr int sum = sum_of(three);
    static constexpr int empty_sum = sum_of(none);
    static_assert(sum == 6 && empty_sum == 0);
    assert(sum_of(three) == 6 && sum_of(none) == 0);
    assert(none.view().size() == 0);
}

}  // namespace

int main() {
    test_push_until_full();
    test_take_last_and_clear();
    test_each_element_ends_once();
    test_full_gives_a_linear_value_back();
    test_list_agrees_with_the_model();
    test_iteration_verbs_of_a_list();
    test_view_of_a_static_list_is_constant();
    crucible::test::pass("test_core_list: all tests passed\n");
    return 0;
}
