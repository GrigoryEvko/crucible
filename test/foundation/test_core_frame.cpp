// Tests of the frame doors of foundation/core/Frame.h: with_mut over each
// owner of the Region family and over each sum of the Choice family.  The
// door of an owner takes the owner into its frame, gives it back, and gives
// the result of a body that gives a value in a Record.  While the body runs,
// the name that the caller moved from holds no storage.  The door of a sum
// writes the active alternative in place.  The frames of two call sites have
// two brands.  A move of a ViewMut, and each door that takes one by rvalue
// reference, leaves no run under the old name.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/List.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::FixedList;
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

// A keyed FixedArray of initial values lends its run in place.  The body
// writes the elements of the array itself, and a body that gives a value
// gives it alone.  The door copies no element: the address of the first
// element in the body is the address of the first element of the array.
void test_fixed_array() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>,
                                                             FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, out.size() == 4);
        for (int& element : out) {
            element *= 10;
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, sum_of(array) == 100);
    int const total = array.with_mut(key, [](auto out) noexcept { return sum_of(out); });
    CRUCIBLE_TEST_ASSERT(kTest, total == 100 && sum_of(array) == 100);
    View<int const, 4> const elements = array.view();
    bool const is_in_place = array.with_mut(
        key, [elements](auto out) noexcept { return offset_of(elements, View<int const>{out.view()}).is_some(); });
    CRUCIBLE_TEST_ASSERT(kTest, is_in_place);
}

// A keyed FixedArray lends its run in place with its key.  The key moves into
// the door and comes back, alone for a body that gives void and after the
// result in a Record for a body that gives a value.  The borrow form keeps the
// key with the caller.  The element door writes one element.  The door copies
// no element, and a copy between the frames of two keyed arrays of two mints
// takes the memcpy of the alias proof.
void test_keyed_array() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    key = array.with_mut(std::move(key), [](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, out.size() == 4);
        int next = 1;
        for (int& element : out) {
            element = next;
            ++next;
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, sum_of(array) == 10);
    auto [total, back] = array.with_mut(std::move(key), [](auto out) noexcept { return sum_of(out); });
    CRUCIBLE_TEST_ASSERT(kTest, total == 10);
    int const lent_total = array.with_mut(back, [](auto out) noexcept { return sum_of(out); });
    CRUCIBLE_TEST_ASSERT(kTest, lent_total == 10);
    View<int const, 4> const elements = array.view();
    bool const is_in_place = array.with_mut(
        back, [elements](auto out) noexcept { return offset_of(elements, View<int const>{out.view()}).is_some(); });
    CRUCIBLE_TEST_ASSERT(kTest, is_in_place);

    back = array.with_element(std::move(back), ::foundation::core::index<2>(),
                              [](int& element) noexcept { element = 30; });
    int const third = array.with_element(back, ::foundation::core::index<2>(), [](int& element) noexcept {
        element += 1;
        return element;
    });
    CRUCIBLE_TEST_ASSERT(kTest, third == 31 && array.at(::foundation::core::index<2>()) == 31);

    // A keyed array copies its elements out with no key, and it moves only as a
    // read of its elements.  It takes no assignment, also from its own type.
    auto const copied = array;
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::equal(copied.view(), array.view()));
    static_assert(!std::is_copy_assignable_v<decltype(array)> && !std::is_move_assignable_v<decltype(array)>);
    static_assert(std::is_trivially_move_constructible_v<decltype(array)>);

    // The maker copies the elements of a plain array.
    auto [listed, listed_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 3>,
                                                                     ::foundation::core::FixedArray<int, 3>{7, 8, 9});
    int const listed_sum = listed.with_mut(listed_key, [](auto out) noexcept { return sum_of(out); });
    CRUCIBLE_TEST_ASSERT(kTest, listed_sum == 24);

    auto [target, target_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto& lent_key = target_key;
    auto& lent_target = target;
    array.with_mut(back, [&lent_target, &lent_key](auto source_view) noexcept {
        lent_target.with_mut(lent_key, [&source_view](auto target_view) noexcept {
            static_assert(
                ::foundation::core::DisjointInput<decltype(source_view), typename decltype(target_view)::brand_type>);
            ::foundation::core::copy(std::move(target_view), source_view).expect("the two runs have one count");
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::equal(target.view(), array.view()));
}

// A HeapArray gives its storage to the frame, so the name that the caller
// moved from holds no element while the body runs.
void test_heap_array() {
    HeapArray<int> array = ::foundation::core::mint_heap_array(allocation(), 1000, 1);
    array = with_mut(std::move(through(array)), [&array](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, through(array).size() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, out.size() == 1000);
        int next = 0;
        for (int& element : out) {
            element = next++;
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, array.size() == 1000 && sum_of(array) == 999 * 1000 / 2);
    auto [first, back] = with_mut(std::move(array), [](auto out) noexcept {
        return out.get(0).match([](auto element) noexcept { return *element; }, [] noexcept { return -1; });
    });
    CRUCIBLE_TEST_ASSERT(kTest, first == 0 && back.size() == 1000);
}

// A MaskedArray moves its storage as a HeapArray does.
void test_masked_array() {
    MaskedArray<int> array = ::foundation::core::mint_masked_array<int>(allocation(), 8);
    array = with_mut(std::move(through(array)), [&array](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, through(array).size() == 0 && out.size() == 8);
        ::foundation::core::fill(std::move(out), 3);
    });
    CRUCIBLE_TEST_ASSERT(kTest, array.at_masked(7) == 3 && array.at_masked(8 + 2) == 3);
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
            static_cast<void>(list.append(Element{counter, label}).expect("the list has room"));
        }
        ends = 0;
        auto const& list_read = list;
        auto taken = with_mut(std::move(through(list)), [&list_read](auto out) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, through(list_read).size() == 0 && out.size() == 3);
            for (Element& element : out) {
                element.label *= 2;
            }
        });
        CRUCIBLE_TEST_ASSERT(kTest, ends == 0 && taken.size() == 3);
        int labels = 0;
        for (Element const& element : taken) {
            labels += element.label;
        }
        CRUCIBLE_TEST_ASSERT(kTest, labels == 12);
    }
    CRUCIBLE_TEST_ASSERT(kTest, ends == 3);
}

// The frame door of an Option runs the arm of its state in place.  A full
// Option gives the RefMut of its payload, and an empty Option runs on_none.
// An owning Option keeps a Box, and the arm writes the boxed object through
// the RefMut of the Box.
void test_option() {
    Option<int> full = ::foundation::core::some(41);
    full.with_mut([](auto value) noexcept { *value += 1; }, [] noexcept { CRUCIBLE_TEST_ASSERT(kTest, false); });
    CRUCIBLE_TEST_ASSERT(kTest, full == ::foundation::core::some(42));
    Option<int> empty{::foundation::core::none};
    bool const is_none = empty.with_mut([](auto) noexcept { return false; }, [] noexcept { return true; });
    CRUCIBLE_TEST_ASSERT(kTest, is_none && empty.is_none());
    Option<::foundation::core::Box<int>> boxed =
        ::foundation::core::some(::foundation::core::mint_box<int>(allocation(), 3));
    int const read = boxed.with_mut(
        [](auto box) noexcept {
            **box *= 7;
            return **box;
        },
        [] noexcept { return -1; });
    CRUCIBLE_TEST_ASSERT(kTest, read == 21);
}

// The frame door of a Result runs the arm of its state in place, with the
// RefMut of the value or of the error.
void test_result() {
    Result<int, LengthMismatch> value = 5;
    value.with_mut([](auto held) noexcept { *held *= 3; }, [](auto) noexcept { CRUCIBLE_TEST_ASSERT(kTest, false); });
    CRUCIBLE_TEST_ASSERT(kTest, std::move(value).expect("the frame keeps the value") == 15);
    Result<int, LengthMismatch> error = ::foundation::core::err(LengthMismatch{});
    bool const is_error = error.with_mut([](auto) noexcept { return false; }, [](auto) noexcept { return true; });
    CRUCIBLE_TEST_ASSERT(kTest, is_error && error.is_err());
}

// The frame door of a OneOf runs the arm of the active alternative in place.
void test_one_of() {
    OneOf<int, LengthMismatch> alternative = 7;
    alternative.with_mut([](auto held) noexcept { *held += 1; },
                         [](auto) noexcept { CRUCIBLE_TEST_ASSERT(kTest, false); });
    CRUCIBLE_TEST_ASSERT(kTest, std::move(alternative).expect<int>("the frame keeps the alternative") == 8);
    OneOf<int, LengthMismatch> other = LengthMismatch{};
    int const index = other.with_mut([](auto) noexcept { return 0; }, [](auto) noexcept { return 1; });
    CRUCIBLE_TEST_ASSERT(kTest, index == 1 && other.is<LengthMismatch>());
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
// frame has the brand of that frame.  A ViewMut does not copy, its end is
// trivial, and its move empties the source, so std::bit_cast builds none.  The
// ViewMut of a FixedArray has the fixed extent of the array, and it holds one
// pointer.
void test_brands_and_shape() {
    auto [outer, outer_key] =
        ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 2>, FixedArray<int, 2>{1, 2});
    HeapArray<int> inner = ::foundation::core::mint_heap_array(allocation(), 2, 3);
    outer.with_mut(outer_key, [&inner](auto outer_view) noexcept {
        inner = with_mut(std::move(inner), [&outer_view](auto inner_view) noexcept {
            using Outer = std::remove_cvref_t<decltype(outer_view)>;
            using Inner = std::remove_cvref_t<decltype(inner_view)>;
            static_assert(!std::is_same_v<typename Outer::brand_type, typename Inner::brand_type>);
            static_assert(!std::is_copy_constructible_v<Inner> && std::is_nothrow_move_constructible_v<Inner>);
            static_assert(!std::is_trivially_move_constructible_v<Inner>);
            static_assert(std::is_trivially_destructible_v<Inner> && !std::is_trivially_copyable_v<Inner>);
            static_assert(sizeof(Inner) == 2 * sizeof(void*));
            static_assert(Outer::extent == 2 && sizeof(Outer) == sizeof(void*));
            static_assert(!std::is_copy_constructible_v<Outer> && std::is_nothrow_move_constructible_v<Outer>);
            static_assert(!std::is_trivially_move_constructible_v<Outer>);
            for (int& element : inner_view) {
                element += sum_of(outer_view);
            }
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, sum_of(inner) == 3 + 3 + 2 * 3);
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
        CRUCIBLE_TEST_ASSERT(kTest, low.size() == 4 && high.size() == 6);
        for (int& element : low) {
            element = 1;
        }
        auto [empty, rest] = split_at(std::move(high), 0).expect("the offset zero is inside each run");
        CRUCIBLE_TEST_ASSERT(kTest, empty.size() == 0 && rest.size() == 6);
        for (int& element : rest) {
            element = 2;
        }
        auto [all, nothing] = split_at(std::move(rest), 6).expect("the count is inside the run");
        CRUCIBLE_TEST_ASSERT(kTest, all.size() == 6 && nothing.size() == 0);
        bool const is_refused = split_at(std::move(low), 5).is_none();
        CRUCIBLE_TEST_ASSERT(kTest, is_refused);
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(run, [](std::size_t position) noexcept { return position < 4 ? 1 : 2; }));

    FixedArray<int, 6> const fixed = ::foundation::core::build_fixed_array<int, 6>([](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        auto [low, high] = split_at<2>(std::move(whole));
        static_assert(decltype(low)::extent == 2 && decltype(high)::extent == 4);
        static_assert(std::is_same_v<typename decltype(high)::brand_type, typename Whole::brand_type>);
        for (int& element : low) {
            element = 5;
        }
        ViewMut<int, typename Whole::brand_type> const dynamic{std::move(high)};
        CRUCIBLE_TEST_ASSERT(kTest, dynamic.size() == 4);
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(fixed, [](std::size_t position) noexcept { return position < 2 ? 5 : 0; }));

    HeapArray<int> chunked = ::foundation::core::mint_heap_array(allocation(), 10, 0);
    chunked = with_mut(std::move(chunked), [](auto whole) noexcept {
        auto chunks = split_chunks(std::move(whole), 4);
        CRUCIBLE_TEST_ASSERT(kTest, chunks.size() == 3);
        for (int mark = 1; mark <= 3; ++mark) {
            auto part = chunks.next().expect("the run holds three chunks");
            CRUCIBLE_TEST_ASSERT(kTest, part.size() == (mark == 3 ? 2u : 4u));
            for (int& element : part) {
                element = mark;
            }
        }
        CRUCIBLE_TEST_ASSERT(kTest, chunks.size() == 0 && chunks.next().is_none());
    });
    CRUCIBLE_TEST_ASSERT(
        kTest, holds_each(chunked, [](std::size_t position) noexcept { return 1 + static_cast<int>(position / 4); }));
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
// input of a different count gives LengthMismatch and writes nothing.  A View
// that names the run of the output writes it in place.  The old name of the
// output names no run, so it is an input of no element.
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
    CRUCIBLE_TEST_ASSERT(kTest,
                         holds_each(out, [](std::size_t position) noexcept { return 3 + static_cast<int>(position); }));

    out = with_mut(std::move(out), [&right](auto out_view) noexcept {
        ::foundation::core::write_each(std::move(out_view), AddOp{}, right.view(), right.view())
            .expect("the three runs have one count");
    });
    CRUCIBLE_TEST_ASSERT(kTest,
                         holds_each(out, [](std::size_t position) noexcept { return 2 * static_cast<int>(position); }));

    out = with_mut(std::move(out), [](auto out_view) noexcept {
        View<int const> const input = out_view.view();
        ::foundation::core::write_each(std::move(out_view), TwiceOp{}, input).expect("the run has its own count");
    });
    CRUCIBLE_TEST_ASSERT(kTest,
                         holds_each(out, [](std::size_t position) noexcept { return 4 * static_cast<int>(position); }));

    out = with_mut(std::move(out), [](auto out_view) noexcept {
        bool const is_refused =
            ::foundation::core::write_each(std::move(through(out_view)), TwiceOp{}, out_view).is_err();
        CRUCIBLE_TEST_ASSERT(kTest, is_refused);
    });
    CRUCIBLE_TEST_ASSERT(kTest,
                         holds_each(out, [](std::size_t position) noexcept { return 4 * static_cast<int>(position); }));

    out = with_mut(std::move(out), [](auto out_view) noexcept {
        auto [low, high] = split_at(std::move(out_view), 50).expect("the half of the run is inside it");
        ::foundation::core::write_each(std::move(low), TwiceOp{}, high).expect("the two halves have one count");
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(out, [](std::size_t position) noexcept {
                             return position < 50 ? 8 * static_cast<int>(position + 50)
                                                  : 4 * static_cast<int>(position);
                         }));

    HeapArray<int> shorter = ::foundation::core::mint_heap_array(allocation(), 10, 7);
    shorter = with_mut(std::move(shorter), [&right](auto out_view) noexcept {
        bool const is_refused = ::foundation::core::write_each(std::move(out_view), TwiceOp{}, right.view()).is_err();
        CRUCIBLE_TEST_ASSERT(kTest, is_refused);
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(shorter, [](std::size_t) noexcept { return 7; }));
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
    CRUCIBLE_TEST_ASSERT(
        kTest, holds_each(target, [](std::size_t position) noexcept { return static_cast<int>(position) * 5; }));

    HeapArray<int> again = ::foundation::core::mint_heap_array(allocation(), 50, 0);
    again = with_mut(std::move(again), [&target](auto again_view) noexcept {
        ::foundation::core::copy(std::move(again_view), target.view()).expect("the two runs have one count");
    });
    CRUCIBLE_TEST_ASSERT(
        kTest, holds_each(again, [](std::size_t position) noexcept { return static_cast<int>(position) * 5; }));

    again = with_mut(std::move(again), [](auto again_view) noexcept {
        View<int const> const input = again_view.view();
        ::foundation::core::copy(std::move(again_view), input).expect("the run has its own count");
    });
    again = with_mut(std::move(again), [](auto again_view) noexcept {
        bool const is_refused = ::foundation::core::copy(std::move(through(again_view)), again_view).is_err();
        CRUCIBLE_TEST_ASSERT(kTest, is_refused);
    });
    CRUCIBLE_TEST_ASSERT(
        kTest, holds_each(again, [](std::size_t position) noexcept { return static_cast<int>(position) * 5; }));

    again = with_mut(std::move(again), [](auto again_view) noexcept {
        auto [low, high] = split_at(std::move(again_view), 25).expect("the half of the run is inside it");
        ::foundation::core::copy(std::move(low), high).expect("the two halves have one count");
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(again, [](std::size_t position) noexcept {
                             return static_cast<int>(position < 25 ? position + 25 : position) * 5;
                         }));

    HeapArray<int> shorter = ::foundation::core::mint_heap_array(allocation(), 5, 1);
    shorter = with_mut(std::move(shorter), [&again](auto shorter_view) noexcept {
        bool const is_refused = ::foundation::core::copy(std::move(shorter_view), again.view()).is_err();
        CRUCIBLE_TEST_ASSERT(kTest, is_refused);
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(shorter, [](std::size_t) noexcept { return 1; }));
}

// get_mut gives the RefMut of one element with the brand of the frame, which
// writes the element in place.  A position at or past the count gives no
// value.  The Option of a RefMut moves its payload, as an owning Option does.
void test_get_mut() {
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 4, 1);
    run = with_mut(std::move(run), [](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        auto element = std::move(whole.get_mut(2)).expect("the position is inside the run");
        static_assert(std::is_same_v<typename decltype(element)::brand_type, typename Whole::brand_type>);
        static_assert(!std::is_copy_constructible_v<decltype(element)>);
        *element = 30;
        CRUCIBLE_TEST_ASSERT(kTest, whole.get_mut(4).is_none());
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(run, [](std::size_t position) noexcept { return position == 2 ? 30 : 1; }));
}

// A reborrow gives the body a ViewMut of the run with the brand of the
// parent, and gives the parent back after the body: alone for a body that
// gives void, and after the result in a Record.  The body gives its ViewMut to
// fill, and the parent writes the run again after the reborrow.
void test_reborrow() {
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 6, 0);
    run = with_mut(std::move(run), [](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        whole = ::foundation::core::reborrow(std::move(whole), [](auto lent) noexcept {
            static_assert(std::is_same_v<decltype(lent), Whole>);
            ::foundation::core::fill(std::move(lent), 4);
        });
        auto [total, back] =
            ::foundation::core::reborrow(std::move(whole), [](auto lent) noexcept { return sum_of(lent); });
        CRUCIBLE_TEST_ASSERT(kTest, total == 24 && back.size() == 6);
        auto first = std::move(back.get_mut(0)).expect("the run is not empty");
        *first = 9;
    });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(run, [](std::size_t position) noexcept { return position == 0 ? 9 : 4; }));
}

// The fill of a ViewMut writes each element of its run, for an element of one
// byte through the memset of the fill of a View, and for a fixed extent too.
void test_fill_of_view_mut() {
    HeapArray<unsigned char> bytes =
        ::foundation::core::mint_heap_array(allocation(), 33, static_cast<unsigned char>(0));
    bytes = with_mut(std::move(bytes), [](auto whole) noexcept {
        ::foundation::core::fill(std::move(whole), static_cast<unsigned char>(7));
    });
    bool is_each_seven = bytes.size() == 33;
    for (unsigned char const byte : bytes) {
        is_each_seven = is_each_seven && byte == 7;
    }
    CRUCIBLE_TEST_ASSERT(kTest, is_each_seven);
    FixedArray<int, 3> const fixed = ::foundation::core::build_fixed_array<int, 3>(
        [](auto whole) noexcept { ::foundation::core::fill(std::move(whole), 5); });
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(fixed, [](std::size_t) noexcept { return 5; }));
}

// take_window gives the part of a run alone, with the brand of the run, and
// no value for a part past the count.  The fixed form gives the fixed extent.
// A body that writes two windows gives the run to the first one through
// reborrow.
void test_take_window() {
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 6, 0);
    run = with_mut(std::move(run), [](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        whole = ::foundation::core::reborrow(std::move(whole), [](auto lent) noexcept {
            auto part = ::foundation::core::take_window(std::move(lent), 1, 2).expect("two elements fit");
            static_assert(std::is_same_v<typename decltype(part)::brand_type, typename Whole::brand_type>);
            static_assert(decltype(part)::extent == ::foundation::core::dynamic_extent);
            CRUCIBLE_TEST_ASSERT(kTest, part.size() == 2);
            ::foundation::core::fill(std::move(part), 4);
        });
        whole = ::foundation::core::reborrow(std::move(whole), [](auto lent) noexcept {
            auto part = ::foundation::core::take_window<3>(std::move(lent), 3).expect("three elements fit");
            static_assert(decltype(part)::extent == 3);
            ::foundation::core::fill(std::move(part), 7);
        });
        whole = ::foundation::core::reborrow(std::move(whole), [](auto lent) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::take_window(std::move(lent), 5, 2).is_none());
        });
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::take_window<2>(std::move(whole), 5).is_none());
    });
    auto const expected = [](std::size_t position) noexcept { return position == 0 ? 0 : position < 3 ? 4 : 7; };
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(run, expected));
}

// for_each_chunk gives the body each whole part of a fixed width, with the
// brand of the run, and gives the rest as a ViewMut.
void test_for_each_chunk() {
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 10, 0);
    run = with_mut(std::move(run), [](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        int part_number = 0;
        auto rest = ::foundation::core::for_each_chunk<4>(std::move(whole), [&part_number](auto part) noexcept {
            static_assert(std::is_same_v<typename decltype(part)::brand_type, typename Whole::brand_type>);
            static_assert(decltype(part)::extent == 4);
            ++part_number;
            ::foundation::core::fill(std::move(part), part_number);
        });
        CRUCIBLE_TEST_ASSERT(kTest, part_number == 2 && rest.size() == 2);
        ::foundation::core::fill(std::move(rest), 9);
    });
    auto const expected = [](std::size_t position) noexcept {
        return position < 8 ? 1 + static_cast<int>(position / 4) : 9;
    };
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(run, expected));
}

// for_each_chunk_pair gives the body each whole part of a fixed width, with
// the brand of the run, and the View of the part at the same position of a
// second run.  It gives the two rests.
void test_for_each_chunk_pair() {
    FixedArray<int, 10> const source = ::foundation::core::build_fixed_array<int, 10>([](auto whole) noexcept {
        int value = 0;
        for (int& element : whole) {
            element = ++value;
        }
    });
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 10, 0);
    run = with_mut(std::move(run), [&source](auto whole) noexcept {
        using Whole = std::remove_cvref_t<decltype(whole)>;
        int part_number = 0;
        auto [rest, source_rest] = ::foundation::core::for_each_chunk_pair<4>(
            std::move(whole), View<int const>{source.view()}, [&part_number](auto part, auto other) noexcept {
                static_assert(std::is_same_v<typename decltype(part)::brand_type, typename Whole::brand_type>);
                static_assert(decltype(part)::extent == 4 && decltype(other)::extent == 4);
                ++part_number;
                ::foundation::core::copy(std::move(part), other);
            });
        CRUCIBLE_TEST_ASSERT(kTest, part_number == 2 && rest.size() == 2 && source_rest.size() == 2);
        ::foundation::core::fill(std::move(rest), 9);
    });
    auto const expected = [](std::size_t position) noexcept {
        return position < 8 ? 1 + static_cast<int>(position) : 9;
    };
    CRUCIBLE_TEST_ASSERT(kTest, holds_each(run, expected));
}

// The copy of a View into a ViewMut of the same fixed extent gives no
// Result, because the type gives the two the same count.  The copy has no
// result to discard, so the call needs no cast to void.
void test_copy_of_fixed_extent() {
    FixedArray<int, 3> const source = ::foundation::core::build_fixed_array<int, 3>([](auto whole) noexcept {
        whole = ::foundation::core::reborrow(std::move(whole),
                                             [](auto lent) noexcept { ::foundation::core::fill(std::move(lent), 2); });
        auto last = std::move(whole.get_mut(2)).expect("three elements");
        *last = 9;
    });
    FixedArray<int, 3> const target = ::foundation::core::build_fixed_array<int, 3>(
        [&source](auto whole) noexcept { ::foundation::core::copy(std::move(whole), source.view()); });
    CRUCIBLE_TEST_ASSERT(kTest,
                         holds_each(target, [](std::size_t position) noexcept { return position == 2 ? 9 : 2; }));
}

// ── The old name of a ViewMut ─────────────────────────────────────────

// Gives Door one ViewMut of ten elements through two parameters, so the rule
// use-after-move does not see that the second one moved.  Door consumes the
// first one.  The kind two-names of the rule view-escape refuses each call that
// gives one unique view to two parameters, so each such call of a test stands
// in a PROBE region.
template <auto Door>
void check_old_name() {
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 10, 1);
    run = with_mut(std::move(run), [](auto whole) noexcept {
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        Door(whole, whole);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    });
}

// The same for a ViewMut of the fixed extent 6, with the View of the run of
// the frame in front.
template <auto Door>
void check_old_name_of_a_fixed_extent() {
    HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), 10, 1);
    run = with_mut(std::move(run), [](auto whole) noexcept {
        View<int const> const elements = whole.view();
        auto part = ::foundation::core::take_window<6>(std::move(whole), 2).expect("six elements fit");
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        Door(elements, part, part);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    });
}

// True when a ViewMut names no element of the run of its frame.  A ViewMut of
// a fixed extent keeps its extent, so its count tells nothing.
template <class Run>
[[nodiscard]] bool names_none_of(View<int const> elements, Run const& run) noexcept {
    return offset_of(elements, View<int const>{run.view()}).is_none();
}

// A move, and each door that takes a ViewMut by rvalue reference or by value,
// leaves no element under the old name.  A door that gives no value leaves none
// either.  A self move keeps the run.
void test_old_name_of_a_dynamic_extent() {
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto const moved = std::move(view);
        CRUCIBLE_TEST_ASSERT(kTest, moved.size() == 10 && old_name.size() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, old_name.get(0).is_none() && old_name.view().size() == 0);
        for ([[maybe_unused]] int const element : old_name)
            CRUCIBLE_TEST_ASSERT(kTest, false);
    }>();
    check_old_name<[](auto& view, auto& same) noexcept {
        view = std::move(same);
        CRUCIBLE_TEST_ASSERT(kTest, view.size() == 10 && sum_of(view) == 10);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto [low, high] = split_at(std::move(view), 4).expect("the offset is inside the run");
        CRUCIBLE_TEST_ASSERT(kTest, old_name.size() == 0);
        auto const move_into = [](auto& target, auto& source, auto& source_name) noexcept {
            target = std::move(source);
            CRUCIBLE_TEST_ASSERT(kTest, target.size() == 6 && source_name.size() == 0);
        };
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        move_into(low, high, high);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        bool const is_refused = split_at(std::move(view), 11).is_none();
        CRUCIBLE_TEST_ASSERT(kTest, is_refused && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto chunks = split_chunks(std::move(view), 4);
        CRUCIBLE_TEST_ASSERT(kTest, chunks.size() == 3 && old_name.size() == 0);
        [](auto& source, auto& source_name) noexcept {
            auto moved = std::move(source);
            CRUCIBLE_TEST_ASSERT(kTest, moved.size() == 3 && source_name.size() == 0 && source_name.next().is_none());
            [](auto& target, auto& other, auto& other_name) noexcept {
                target = std::move(other);
                CRUCIBLE_TEST_ASSERT(kTest, target.size() == 3 && other_name.size() == 0);
            }(source, moved, moved);
        }(chunks, chunks);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto const part = ::foundation::core::take_window(std::move(view), 1, 2).expect("two elements fit");
        CRUCIBLE_TEST_ASSERT(kTest, part.size() == 2 && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        bool const is_refused = ::foundation::core::take_window(std::move(view), 9, 2).is_none();
        CRUCIBLE_TEST_ASSERT(kTest, is_refused && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto const part = ::foundation::core::take_window<3>(std::move(view), 7).expect("three elements fit");
        CRUCIBLE_TEST_ASSERT(kTest, sum_of(part) == 3 && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto const rest = ::foundation::core::for_each_chunk<4>(std::move(view), [&old_name](auto part) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, part.size() == 4 && old_name.size() == 0);
        });
        CRUCIBLE_TEST_ASSERT(kTest, rest.size() == 2 && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto const front = ::foundation::core::take_prefix_up_to(std::move(view), 3);
        CRUCIBLE_TEST_ASSERT(kTest, front.size() == 3 && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto const erased = ::foundation::core::erase_brand(std::move(view));
        CRUCIBLE_TEST_ASSERT(kTest, erased.size() == 10 && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        FixedArray<int, 10> const other{};
        auto const [rest, other_rest] = ::foundation::core::for_each_chunk_pair<4>(
            std::move(view), View<int const>{other.view()}, [&old_name](auto part, auto other_part) noexcept {
                CRUCIBLE_TEST_ASSERT(kTest, part.size() == other_part.size() && old_name.size() == 0);
            });
        CRUCIBLE_TEST_ASSERT(kTest, rest.size() == 2 && other_rest.size() == 2 && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        auto const back = ::foundation::core::reborrow(std::move(view), [&old_name](auto lent) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, lent.size() == 10 && old_name.size() == 0);
        });
        CRUCIBLE_TEST_ASSERT(kTest, back.size() == 10 && old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        ::foundation::core::fill(std::move(view), 3);
        CRUCIBLE_TEST_ASSERT(kTest, old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        FixedArray<int, 10> const source{};
        ::foundation::core::copy(std::move(view), View<int const>{source.view()}).expect("ten elements");
        CRUCIBLE_TEST_ASSERT(kTest, old_name.size() == 0);
    }>();
    check_old_name<[](auto& view, auto& old_name) noexcept {
        FixedArray<int, 10> const source{};
        ::foundation::core::write_each(std::move(view), TwiceOp{}, View<int const>{source.view()})
            .expect("ten elements");
        CRUCIBLE_TEST_ASSERT(kTest, old_name.size() == 0);
    }>();
    HeapArray<unsigned char> bytes =
        ::foundation::core::mint_heap_array(allocation(), 5, static_cast<unsigned char>(1));
    bytes = with_mut(std::move(bytes), [](auto whole) noexcept {
        auto const view_the_bytes = [](auto& view, auto& old_name) noexcept {
            auto const storage = ::foundation::core::byte_view(std::move(view));
            CRUCIBLE_TEST_ASSERT(kTest, storage.size() == 5 && old_name.size() == 0);
        };
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        view_the_bytes(whole, whole);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    });
}

// The old name of a fixed extent names no element of the run, and the target
// names the part that the window gave.
void test_old_name_of_a_fixed_extent() {
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        auto const moved = std::move(view);
        CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, moved) && names_none_of(elements, old_name));
    }>();
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& same) noexcept {
        view = std::move(same);
        CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, view));
    }>();
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        using Dynamic = ViewMut<int, typename std::remove_cvref_t<decltype(view)>::brand_type>;
        Dynamic const dynamic{std::move(view)};
        CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, dynamic) && names_none_of(elements, old_name));
    }>();
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        auto const [low, high] = split_at<2>(std::move(view));
        CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, low) && !names_none_of(elements, high));
        CRUCIBLE_TEST_ASSERT(kTest, names_none_of(elements, old_name));
    }>();
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        auto const part = ::foundation::core::take_window<2>(std::move(view), 1).expect("two elements fit");
        CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, part) && names_none_of(elements, old_name));
    }>();
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        auto const erased = ::foundation::core::erase_brand(std::move(view));
        CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, erased) && names_none_of(elements, old_name));
    }>();
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        auto const back = ::foundation::core::reborrow(std::move(view), [elements, &old_name](auto lent) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, lent) && names_none_of(elements, old_name));
        });
        CRUCIBLE_TEST_ASSERT(kTest, !names_none_of(elements, back) && names_none_of(elements, old_name));
    }>();
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        ::foundation::core::fill(std::move(view), 3);
        CRUCIBLE_TEST_ASSERT(kTest, names_none_of(elements, old_name));
    }>();
    // A door whose result keeps the extent gives an emptied result.
    check_old_name_of_a_fixed_extent<[](View<int const> elements, auto& view, auto& old_name) noexcept {
        auto const moved = std::move(view);
        static_cast<void>(moved);
        auto const erased = ::foundation::core::erase_brand(std::move(old_name));
        CRUCIBLE_TEST_ASSERT(kTest, names_none_of(elements, erased));
    }>();
}

// The text of the end of a door that reads a run that its view gave up.
constexpr ::fixy::TextView kGivenUp{"a ViewMut gave its run up, and a door reads the run under its old name"};

// The text of the end of a read of a run of more than 4096 bytes that its view
// gave up.  Each door reads such a run through its first element, so this test
// ends the process before each other one.
constexpr ::fixy::TextView kLargeGivenUp{"foundation: fatal invariant failed: first_ != nullptr"};

// Moves a ViewMut of the fixed extent Count out of its name, and gives the old
// name to Door.  Two parameters name the view, so the rule use-after-move does
// not see that the old name moved.  Door ends the process in a child.  The call
// that gives the view to the two parameters stands in a PROBE region.
template <auto Door, std::size_t Count = 6>
void expect_given_up() {
    constexpr bool is_large = Count * sizeof(int) > 4096;
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            HeapArray<int> run = ::foundation::core::mint_heap_array(allocation(), Count + 4, 1);
            run = with_mut(std::move(run), [](auto whole) noexcept {
                auto part = ::foundation::core::take_window<Count>(std::move(whole), 2).expect("the window fits");
                auto const give_up_and_read = [](auto& view, auto& old_name) noexcept {
                    auto const moved = std::move(view);
                    static_cast<void>(moved);
                    Door(old_name);
                };
                CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
                give_up_and_read(part, part);
                CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
            });
        },
        is_large ? kLargeGivenUp : kGivenUp);
}

// A door that gives a part of the run or drops the fixed extent, and a door
// that trusts two brands, ends the process for a ViewMut of a fixed extent that
// gave its run up.  Its extent tells six elements, and it names none.
void test_given_up_run_ends_the_process() {
    expect_given_up<[](auto& old_name) noexcept { static_cast<void>(split_at<2>(std::move(old_name))); }>();
    expect_given_up<[](auto& old_name) noexcept { static_cast<void>(split_at(std::move(old_name), 2)); }>();
    expect_given_up<[](auto& old_name) noexcept { static_cast<void>(split_chunks(std::move(old_name), 4)); }>();
    expect_given_up<[](auto& old_name) noexcept {
        static_cast<void>(::foundation::core::take_window(std::move(old_name), 1, 2));
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        static_cast<void>(::foundation::core::take_window<2>(std::move(old_name), 1));
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        static_cast<void>(::foundation::core::for_each_chunk<2>(std::move(old_name), [](auto) noexcept {}));
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        static_cast<void>(::foundation::core::take_prefix_up_to(std::move(old_name), 0));
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        FixedArray<int, 6> const other{};
        static_cast<void>(::foundation::core::for_each_chunk_pair<2>(std::move(old_name), View<int const>{other.view()},
                                                                     [](auto, auto) noexcept {}));
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        using Dynamic = ViewMut<int, typename std::remove_cvref_t<decltype(old_name)>::brand_type>;
        Dynamic const dynamic{std::move(old_name)};
        static_cast<void>(dynamic);
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        HeapArray<int> other = ::foundation::core::mint_heap_array(allocation(), 6, 0);
        other = with_mut(std::move(other), [&old_name](auto out) noexcept {
            ::foundation::core::write_each(std::move(out), TwiceOp{}, old_name).expect("six elements");
        });
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        HeapArray<int> other = ::foundation::core::mint_heap_array(allocation(), 6, 0);
        other = with_mut(std::move(other), [&old_name](auto in) noexcept {
            ::foundation::core::write_each(std::move(old_name), TwiceOp{}, in).expect("six elements");
        });
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        HeapArray<int> other = ::foundation::core::mint_heap_array(allocation(), 6, 0);
        other = with_mut(std::move(other), [&old_name](auto out) noexcept {
            ::foundation::core::copy(std::move(out), old_name).expect("six elements");
        });
    }>();
    expect_given_up<[](auto& old_name) noexcept {
        HeapArray<int> other = ::foundation::core::mint_heap_array(allocation(), 6, 0);
        other = with_mut(std::move(other), [&old_name](auto in) noexcept {
            ::foundation::core::copy(std::move(old_name), in).expect("six elements");
        });
    }>();
}

// A run of more bytes than the page at the address zero, 4096: an index into it
// can name mapped memory, so each read of the old name ends the process.
constexpr std::size_t kLarge = 2 * 4096 / sizeof(int);

void test_given_up_large_run_ends_the_process() {
    expect_given_up<[](auto& old_name) noexcept { static_cast<void>(old_name.get(kLarge - 1).is_some()); }, kLarge>();
    expect_given_up<[](auto& old_name) noexcept { static_cast<void>(old_name.get_mut(kLarge - 1).is_some()); },
                    kLarge>();
    expect_given_up<[](auto& old_name) noexcept { CRUCIBLE_TEST_ASSERT(kTest, sum_of(old_name) == 0); }, kLarge>();
    expect_given_up<[](auto& old_name) noexcept { static_cast<void>(old_name.view().size()); }, kLarge>();
    expect_given_up<[](auto& old_name) noexcept { ::foundation::core::fill(std::move(old_name), 3); }, kLarge>();
    constexpr auto lend_it = [](auto& old_name) noexcept {
        static_cast<void>(::foundation::core::reborrow(std::move(old_name), [](auto) noexcept {}));
    };
    expect_given_up<lend_it, kLarge>();
    constexpr auto copy_into_it = [](auto& old_name) noexcept {
        HeapArray<int> const source = ::foundation::core::mint_heap_array(allocation(), kLarge, 0);
        ::foundation::core::copy(std::move(old_name), source.view()).expect("one count");
    };
    expect_given_up<copy_into_it, kLarge>();
    constexpr auto write_into_it = [](auto& old_name) noexcept {
        HeapArray<int> const source = ::foundation::core::mint_heap_array(allocation(), kLarge, 0);
        ::foundation::core::write_each(std::move(old_name), TwiceOp{}, source.view()).expect("one count");
    };
    expect_given_up<write_into_it, kLarge>();
}

}  // namespace

int main() {
    // A body that captures a closure of main by a reference that is not const
    // passes the store check of a frame door (foundation/core/CaptureStore.h).
    // The walk reads the closure as a writable object, and its test of the
    // base goes up through the scopes of the closure.  GCC gives main no
    // parent, so the test stops at main, and the closure is no class of the
    // base.
    int calls = 0;
    auto count_call = [&calls]() noexcept { ++calls; };
    auto [pair, pair_key] =
        ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 2>, FixedArray<int, 2>{3, 4});
    int const pair_total = pair.with_mut(pair_key, [&count_call](auto out) noexcept {
        count_call();
        return sum_of(out);
    });
    CRUCIBLE_TEST_ASSERT(kTest, pair_total == 7 && calls == 1);
    test_fixed_array();
    test_keyed_array();
    test_heap_array();
    test_masked_array();
    test_fixed_list();
    test_option();
    test_result();
    test_one_of();
    test_brands_and_shape();
    test_splits();
    test_write_each();
    test_copy_between_frames();
    test_get_mut();
    test_reborrow();
    test_fill_of_view_mut();
    test_take_window();
    test_for_each_chunk();
    test_for_each_chunk_pair();
    test_copy_of_fixed_extent();
    test_old_name_of_a_dynamic_extent();
    test_old_name_of_a_fixed_extent();
    test_given_up_run_ends_the_process();
    test_given_up_large_run_ends_the_process();
    return 0;
}
