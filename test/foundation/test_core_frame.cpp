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
using ::foundation::core::split_at;
using ::foundation::core::split_chunks;
using ::foundation::core::ViewMut;
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

// True when each element of a run holds the value that expected gives for its
// position.
template <class Run, class Expected>
[[nodiscard]] bool holds_each(Run const& run, Expected expected) noexcept {
    std::size_t position = 0;
    for (int const element : run) {
        if (element != expected(position)) return false;
        ++position;
    }
    return position == run.size();
}

// The frames of two call sites have two brands, and each unique borrow of a
// frame has the brand of that frame.  A ViewMut does not copy, its move and
// its end are trivial, and its assignment is not, so the ABI passes it in two
// registers and std::bit_cast builds none.  The ViewMut of a FixedArray has
// the fixed extent of the array, and the ABI passes it in one register.
void test_brands_and_shape() {
    FixedArray<int, 2> outer{1, 2};
    HeapArray<int> inner = ::foundation::core::mint_heap_array(allocation(), 2, 3);
    outer = with_mut(std::move(outer), [&inner](auto outer_view) noexcept {
        inner = with_mut(std::move(inner), [&outer_view](auto inner_view) noexcept {
            using Outer = std::remove_cvref_t<decltype(outer_view)>;
            using Inner = std::remove_cvref_t<decltype(inner_view)>;
            static_assert(!std::is_same_v<typename Outer::brand_type, typename Inner::brand_type>);
            static_assert(!std::is_copy_constructible_v<Inner> && std::is_trivially_move_constructible_v<Inner>);
            static_assert(std::is_trivially_destructible_v<Inner> && !std::is_trivially_copyable_v<Inner>);
            static_assert(sizeof(Inner) == 2 * sizeof(void*));
            static_assert(Outer::extent == 2 && sizeof(Outer) == sizeof(void*));
            static_assert(!std::is_copy_constructible_v<Outer> && std::is_trivially_move_constructible_v<Outer>);
            for (int& element : inner_view) {
                element += sum_of(outer_view);
            }
        });
    });
    assert(sum_of(inner) == 3 + 3 + 2 * 3);
}

// A split at an offset gives the two parts of the run, each with the brand of
// the frame, and a write through each part lands in its own elements of the
// owner.  An offset past the count gives no value, and the offsets zero and
// the count give an empty part.  The split of a ViewMut of a fixed extent at
// a constant offset gives two parts of fixed extents, and a fixed part
// converts to the ViewMut of a dynamic extent.  A split into chunks gives
// each chunk one time, and a last chunk of the rest.
void test_splits() {
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 10, 0);
    run = with_mut(std::move(run), [](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        auto [low, high] = split_at(std::move(whole), 4).expect("the offset is inside the run");
        static_assert(std::is_same_v<typename decltype(low)::brand_type, typename Whole::brand_type>);
        static_assert(std::is_same_v<decltype(high), Whole>);
        assert(low.size() == 4 && high.size() == 6);
        for (int& element : low) {
            element = 1;
        }
        auto [empty, rest] = split_at(std::move(high), 0).expect("the offset zero is inside each run");
        assert(empty.size() == 0 && rest.size() == 6);
        for (int& element : rest) {
            element = 2;
        }
        auto [all, nothing] = split_at(std::move(rest), 6).expect("the count is inside the run");
        assert(all.size() == 6 && nothing.size() == 0);
        bool const is_refused = split_at(std::move(low), 5).is_none();
        assert(is_refused);
    });
    assert(holds_each(run, [](std::size_t position) noexcept { return position < 4 ? 1 : 2; }));

    FixedArray<int, 6> fixed{};
    fixed = with_mut(std::move(fixed), [](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        auto [low, high] = split_at<2>(std::move(whole));
        static_assert(decltype(low)::extent == 2 && decltype(high)::extent == 4);
        static_assert(std::is_same_v<typename decltype(high)::brand_type, typename Whole::brand_type>);
        for (int& element : low) {
            element = 5;
        }
        ViewMut<int, typename Whole::brand_type> const dynamic{std::move(high)};
        assert(dynamic.size() == 4);
    });
    assert(holds_each(fixed, [](std::size_t position) noexcept { return position < 2 ? 5 : 0; }));

    HeapArray<int> chunked = ::foundation::core::mint_heap_array(allocation(), 10, 0);
    chunked = with_mut(std::move(chunked), [](auto whole) noexcept {
        auto chunks = split_chunks(std::move(whole), 4);
        assert(chunks.size() == 3);
        for (int mark = 1; mark <= 3; ++mark) {
            auto part = chunks.next().expect("the run holds three chunks");
            assert(part.size() == (mark == 3 ? 2u : 4u));
            for (int& element : part) {
                element = mark;
            }
        }
        assert(chunks.size() == 0 && chunks.next().is_none());
    });
    assert(holds_each(chunked, [](std::size_t position) noexcept { return 1 + static_cast<int>(position / 4); }));
}

// The ops of the doors of a kernel.  Each holds no state.
struct AddOp final {
    int operator()(int const& left, int const& right) const noexcept { return left + right; }
};
struct TwiceOp final {
    int operator()(int const& value) const noexcept { return 2 * value; }
};

// The door write_each over inputs of two other frames takes the loop of
// restrict pointers, and over a View or an input of the frame of the output
// the loop of plain pointers.  The two loops give the same values, and an
// input of a different count gives LengthMismatch and writes nothing.
void test_write_each() {
    HeapArray<int> left = ::foundation::core::mint_heap_array(allocation(), 100, 3);
    HeapArray<int> right = ::foundation::core::mint_heap_array_with<int>(
        allocation(), 100, [](std::size_t position) noexcept { return static_cast<int>(position); });
    HeapArray<int> out = ::foundation::core::mint_heap_array(allocation(), 100, 0);
    left = with_mut(std::move(left), [&right, &out](auto left_view) noexcept {
        right = with_mut(std::move(right), [&left_view, &out](auto right_view) noexcept {
            out = with_mut(std::move(out), [&left_view, &right_view](auto out_view) noexcept {
                using Out = std::remove_cvref_t<decltype(out_view)>;
                using Left = std::remove_cvref_t<decltype(left_view)>;
                static_assert(::foundation::core::DisjointInput<Left, typename Out::brand_type>);
                static_assert(!::foundation::core::DisjointInput<Out, typename Out::brand_type>);
                static_assert(!::foundation::core::DisjointInput<View<int const>, typename Out::brand_type>);
                ::foundation::core::write_each(std::move(out_view), AddOp{}, left_view, right_view)
                    .expect("the three runs have one count");
            });
        });
    });
    assert(holds_each(out, [](std::size_t position) noexcept { return 3 + static_cast<int>(position); }));

    out = with_mut(std::move(out), [&right](auto out_view) noexcept {
        ::foundation::core::write_each(std::move(out_view), AddOp{}, right.view(), right.view())
            .expect("the three runs have one count");
    });
    assert(holds_each(out, [](std::size_t position) noexcept { return 2 * static_cast<int>(position); }));

    out = with_mut(std::move(out), [](auto out_view) noexcept {
        ::foundation::core::write_each(std::move(through(out_view)), TwiceOp{}, out_view)
            .expect("the run has its own count");
    });
    assert(holds_each(out, [](std::size_t position) noexcept { return 4 * static_cast<int>(position); }));

    HeapArray<int> shorter = ::foundation::core::mint_heap_array(allocation(), 10, 7);
    shorter = with_mut(std::move(shorter), [&right](auto out_view) noexcept {
        bool const is_refused = ::foundation::core::write_each(std::move(out_view), TwiceOp{}, right.view()).is_err();
        assert(is_refused);
    });
    assert(holds_each(shorter, [](std::size_t) noexcept { return 7; }));
}

// The door copy between two frames is one memcpy, and from a View or from the
// frame of the output one memmove.  A source of a different count gives
// LengthMismatch and writes nothing.
void test_copy_between_frames() {
    HeapArray<int> source = ::foundation::core::mint_heap_array_with<int>(
        allocation(), 50, [](std::size_t position) noexcept { return static_cast<int>(position) * 5; });
    HeapArray<int> target = ::foundation::core::mint_heap_array(allocation(), 50, 0);
    source = with_mut(std::move(source), [&target](auto source_view) noexcept {
        target = with_mut(std::move(target), [&source_view](auto target_view) noexcept {
            ::foundation::core::copy(std::move(target_view), source_view).expect("the two runs have one count");
        });
    });
    assert(holds_each(target, [](std::size_t position) noexcept { return static_cast<int>(position) * 5; }));

    HeapArray<int> again = ::foundation::core::mint_heap_array(allocation(), 50, 0);
    again = with_mut(std::move(again), [&target](auto again_view) noexcept {
        ::foundation::core::copy(std::move(again_view), target.view()).expect("the two runs have one count");
    });
    assert(holds_each(again, [](std::size_t position) noexcept { return static_cast<int>(position) * 5; }));

    again = with_mut(std::move(again), [](auto again_view) noexcept {
        ::foundation::core::copy(std::move(through(again_view)), again_view).expect("the run has its own count");
    });
    assert(holds_each(again, [](std::size_t position) noexcept { return static_cast<int>(position) * 5; }));

    HeapArray<int> shorter = ::foundation::core::mint_heap_array(allocation(), 5, 1);
    shorter = with_mut(std::move(shorter), [&again](auto shorter_view) noexcept {
        bool const is_refused = ::foundation::core::copy(std::move(shorter_view), again.view()).is_err();
        assert(is_refused);
    });
    assert(holds_each(shorter, [](std::size_t) noexcept { return 1; }));
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
    test_splits();
    test_write_each();
    test_copy_between_frames();
    return 0;
}
