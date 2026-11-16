// Tests of the frame doors of foundation/core/Frame.h: with_mut over each
// owner of the Region family and over each sum of the Choice family.  The
// door of an owner takes the owner into its frame, gives it back, and gives
// the result of a body that gives a value in a Record.  While the body runs,
// the name that the caller moved from holds no storage.  The door of a sum
// writes the active alternative in place.  The frames of two call sites have
// two brands, and a ViewMut keeps the call ABI of two registers.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Grow.h>
#include <foundation/core/List.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include "../test_assert.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::FixedList;
using ::foundation::core::GrowList;
using ::foundation::core::HeapArray;
using ::foundation::core::LengthMismatch;
using ::foundation::core::MaskedArray;
using ::foundation::core::OneOf;
using ::foundation::core::Option;
using ::foundation::core::Result;
using ::foundation::core::View;
using ::foundation::core::with_mut;

[[nodiscard]] ::foundation::effects::Alloc allocation() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The owner that a call gives back by reference.  The rule use-after-move
// does not read through it, so a test that moves an owner through it reads
// the name that the caller moved from in the body.
template <class Owner>
[[nodiscard]] Owner& through(Owner& owner) noexcept {
    return owner;
}

// The sum of the elements of a run.
template <class Run>
[[nodiscard]] int sum_of(Run const& run) noexcept {
    int total = 0;
    for (int const element : run) {
        total += element;
    }
    return total;
}

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

// A FixedArray moves into the frame by a copy of its elements.  The body
// writes the copy, and the caller takes the written array back.  A body that
// gives a value gives it in a Record before the array.
void test_fixed_array() {
    FixedArray<int, 4> array{1, 2, 3, 4};
    array = with_mut(std::move(array), [](auto out) noexcept {
        assert(out.size() == 4);
        for (int& element : out) {
            element *= 10;
        }
    });
    assert(sum_of(array) == 100);
    auto [total, back] = with_mut(std::move(array), [](auto out) noexcept { return sum_of(out); });
    assert(total == 100 && sum_of(back) == 100);
}

// A HeapArray gives its storage to the frame, so the name that the caller
// moved from holds no element while the body runs.
void test_heap_array() {
    HeapArray<int> array = ::foundation::core::mint_heap_array(allocation(), 1000, 1);
    array = with_mut(std::move(through(array)), [&array](auto out) noexcept {
        assert(through(array).size() == 0);
        assert(out.size() == 1000);
        int next = 0;
        for (int& element : out) {
            element = next++;
        }
    });
    assert(array.size() == 1000 && sum_of(array) == 999 * 1000 / 2);
    auto [first, back] = with_mut(std::move(array), [](auto out) noexcept {
        return out.get(0).match([](auto element) noexcept { return *element; }, [] noexcept { return -1; });
    });
    assert(first == 0 && back.size() == 1000);
}

// A MaskedArray moves its storage as a HeapArray does.
void test_masked_array() {
    MaskedArray<int> array = ::foundation::core::mint_masked_array<int>(allocation(), 8);
    array = with_mut(std::move(through(array)), [&array](auto out) noexcept {
        assert(through(array).size() == 0 && out.size() == 8);
        ::foundation::core::fill(out.view(), 3);
    });
    assert(array.at_masked(7) == 3 && array.at_masked(8 + 2) == 3);
}

// A FixedList moves each element into the frame and back.  Each element
// ends one time, and the moved-from list holds no element.
void test_fixed_list() {
    int ends = 0;
    auto const counter = ::foundation::core::to(ends);
    using Element = Counted<std::remove_const_t<decltype(counter)>>;
    {
        FixedList<Element, 4> list{};
        for (int label = 1; label <= 3; ++label) {
            static_cast<void>(list.push(Element{counter, label}).expect("the list has room"));
        }
        ends = 0;
        auto taken = with_mut(std::move(through(list)), [&list](auto out) noexcept {
            assert(through(list).size() == 0 && out.size() == 3);
            for (Element& element : out) {
                element.label *= 2;
            }
        });
        assert(ends == 0 && taken.size() == 3);
        int labels = 0;
        for (Element const& element : taken) {
            labels += element.label;
        }
        assert(labels == 12);
    }
    assert(ends == 3);
}

// A GrowList gives its storage to the frame and lends it for the time of the
// body.  After the frame, the list grows again.
void test_grow_list() {
    GrowList<int> list = ::foundation::core::mint_grow_list<int>(10000);
    for (int value = 0; value < 100; ++value) {
        list.append(allocation(), value).expect("the list is below its maximum");
    }
    list = with_mut(std::move(through(list)), [&list](auto out) noexcept {
        assert(through(list).size() == 0 && out.size() == 100);
        for (int& element : out) {
            element = -element;
        }
    });
    for (int value = 0; value < 900; ++value) {
        list.append(allocation(), 1).expect("the list is below its maximum");
    }
    int const total = list.with_view([](View<int const> elements) noexcept { return sum_of(elements); });
    assert(list.size() == 1000 && total == 900 - 99 * 100 / 2);
}

// The frame door of an Option runs the arm of its state in place.  A full
// Option gives the RefMut of its payload, and an empty Option runs on_none.
// An owning Option keeps a Box, and the arm writes the boxed object through
// the RefMut of the Box.
void test_option() {
    Option<int> full = ::foundation::core::some(41);
    full.with_mut([](auto value) noexcept { *value += 1; }, [] noexcept { assert(false); });
    assert(full == ::foundation::core::some(42));
    Option<int> empty{::foundation::core::none};
    bool const is_none = empty.with_mut([](auto) noexcept { return false; }, [] noexcept { return true; });
    assert(is_none && empty.is_none());
    Option<::foundation::core::Box<int>> boxed =
        ::foundation::core::some(::foundation::core::mint_box<int>(allocation(), 3));
    int const read = boxed.with_mut(
        [](auto box) noexcept {
            **box *= 7;
            return **box;
        },
        [] noexcept { return -1; });
    assert(read == 21);
}

// The frame door of a Result runs the arm of its state in place, with the
// RefMut of the value or of the error.
void test_result() {
    Result<int, LengthMismatch> value = 5;
    value.with_mut([](auto held) noexcept { *held *= 3; }, [](auto) noexcept { assert(false); });
    assert(std::move(value).expect("the frame keeps the value") == 15);
    Result<int, LengthMismatch> error = ::foundation::core::err(LengthMismatch{});
    bool const is_error = error.with_mut([](auto) noexcept { return false; }, [](auto) noexcept { return true; });
    assert(is_error && error.is_err());
}

// The frame door of a OneOf runs the arm of the active alternative in place.
void test_one_of() {
    OneOf<int, LengthMismatch> alternative = 7;
    alternative.with_mut([](auto held) noexcept { *held += 1; }, [](auto) noexcept { assert(false); });
    assert(std::move(alternative).expect<int>("the frame keeps the alternative") == 8);
    OneOf<int, LengthMismatch> other = LengthMismatch{};
    int const index = other.with_mut([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
    assert(index == 1 && other.is<LengthMismatch>());
}

// The frames of two call sites have two brands, and each unique borrow of a
// frame has the brand of that frame.  A ViewMut does not copy, its move and
// its end are trivial, and its assignment is not, so the ABI passes it in two
// registers and std::bit_cast builds none.
void test_brands_and_shape() {
    FixedArray<int, 2> outer{1, 2};
    FixedArray<int, 2> inner{3, 4};
    outer = with_mut(std::move(outer), [&inner](auto outer_view) noexcept {
        inner = with_mut(std::move(inner), [&outer_view](auto inner_view) noexcept {
            using Outer = std::remove_cvref_t<decltype(outer_view)>;
            using Inner = std::remove_cvref_t<decltype(inner_view)>;
            static_assert(!std::is_same_v<typename Outer::brand_type, typename Inner::brand_type>);
            static_assert(!std::is_copy_constructible_v<Inner> && std::is_trivially_move_constructible_v<Inner>);
            static_assert(std::is_trivially_destructible_v<Inner> && !std::is_trivially_copyable_v<Inner>);
            static_assert(sizeof(Inner) == 2 * sizeof(void*));
            for (int& element : inner_view) {
                element += sum_of(outer_view);
            }
        });
    });
    assert(sum_of(inner) == 3 + 4 + 2 * 3);
}

}  // namespace

int main() {
    test_fixed_array();
    test_heap_array();
    test_masked_array();
    test_fixed_list();
    test_grow_list();
    test_option();
    test_result();
    test_one_of();
    test_brands_and_shape();
    return 0;
}
