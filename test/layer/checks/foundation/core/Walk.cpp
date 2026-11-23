// The compile-time checks of foundation/core/Walk.h.  Each value check runs
// one chain by constant evaluation, and the file compiles only when each
// value is right.

#include <foundation/core/Walk.h>

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::walk_checks {

// The runs of the checks: constant arrays of internal linkage.
constexpr FixedArray<int, 6> numbers{1, 2, 3, 4, 5, 6};
constexpr FixedArray<int, 3> left_numbers{1, 2, 3};
constexpr FixedArray<int, 3> right_numbers{6, 5, 4};
constexpr FixedArray<int, 4> lengths{1, 2, 3, 4};

// The View of a dynamic extent of a run.
template <std::size_t N>
[[nodiscard]] consteval View<int const> view_of(FixedArray<int, N> const& run) noexcept {
    return run.view();
}

// The named functions of the checks.  A named type keeps the type of a
// stage nameable in the checks of the surface.
struct IsEven final {
    [[nodiscard]] constexpr bool operator()(int const& value) const noexcept { return value % 2 == 0; }
};

struct Twice final {
    [[nodiscard]] constexpr int operator()(int const& value) const noexcept { return value * 2; }
};

struct Sum final {
    [[nodiscard]] constexpr int operator()(int sum, int const& value) const noexcept { return sum + value; }
};

struct Digits final {
    [[nodiscard]] constexpr int operator()(int digits, int const& value) const noexcept { return digits * 10 + value; }
};

// A step that is not commutative gives 123 only in the left order:
// ((0 * 10 + 1) * 10 + 2) * 10 + 3.
static_assert(view_of(left_numbers).walk().fold(0, Digits{}) == 123);
static_assert(View<int const>{}.walk().fold(7, Digits{}) == 7);

// limit and skip of an exact walk keep it exact.
static_assert(view_of(numbers).walk().limit(ElementCount{3}).fold(0, Sum{}) == 1 + 2 + 3);
static_assert(view_of(numbers).walk().skip(ElementCount{4}).fold(0, Sum{}) == 5 + 6);
static_assert(view_of(numbers).walk().limit(ElementCount{9}).size() == 6);
static_assert(view_of(numbers).walk().skip(ElementCount{9}).size() == 0);
static_assert(view_of(numbers).walk().skip(ElementCount{1}).limit(ElementCount{2}).fold(0, Digits{}) == 23);
static_assert(numbers.view().walk().map(Twice{}).limit(ElementCount{4}).size() == 4);
static_assert(numbers.view().walk().map(Twice{}).skip(ElementCount{2}).fold(0, Sum{}) == 2 * (3 + 4 + 5 + 6));

// filter gives a walk of unknown count, so count() walks each element.
static_assert(view_of(numbers).walk().filter(IsEven{}).count() == 3);
static_assert(view_of(numbers).walk().filter(IsEven{}).fold(0, Digits{}) == 246);
static_assert(View<int const>{}.walk().filter(IsEven{}).count() == 0);

// A limit of a walk of unknown count stops after count elements, and a stop
// of any_of reaches the caller.  The evens are 2, 4 and 6.
[[nodiscard]] consteval bool limited_any(std::size_t limit, int wanted) {
    return view_of(numbers)
        .walk()
        .filter(IsEven{})
        .limit(ElementCount{limit})
        .any_of([wanted](int const& value) noexcept { return value == wanted; });
}
static_assert(limited_any(2, 4));
static_assert(!limited_any(2, 6));
static_assert(limited_any(3, 6));
static_assert(!limited_any(0, 2));
static_assert(view_of(numbers).walk().filter(IsEven{}).limit(ElementCount{2}).count() == 2);
static_assert(view_of(numbers).walk().filter(IsEven{}).limit(ElementCount{2}).fold(0, Digits{}) == 24);

// all_of stops at the first refusal, and an empty walk gives true.
[[nodiscard]] consteval bool all_below(std::size_t limit, int bound) {
    return view_of(numbers)
        .walk()
        .filter([](int const& value) noexcept { return value > 0; })
        .limit(ElementCount{limit})
        .all_of([bound](int const& value) noexcept { return value < bound; });
}
static_assert(all_below(3, 4));
static_assert(!all_below(4, 4));
static_assert(all_below(0, 0));
static_assert(!View<int const>{}.walk().any_of(IsEven{}));
static_assert(View<int const>{}.walk().all_of(IsEven{}));
static_assert(view_of(numbers).walk().any_of(IsEven{}) && !view_of(numbers).walk().all_of(IsEven{}));

// zip of two walks of one fixed extent, and a map of the pair: 1*6 + 2*5 + 3*4.
[[nodiscard]] consteval int zip_dot() {
    return left_numbers.view()
        .walk()
        .zip(right_numbers.view().walk())
        .map([](auto const& pair) noexcept {
            auto const& [first, second] = pair;
            return *first * *second;
        })
        .fold(0, Sum{});
}
static_assert(zip_dot() == 28);

// try_zip compares the counts of two walks of a dynamic extent.
[[nodiscard]] consteval int try_zip_dot(std::size_t count) {
    return view_of(left_numbers)
        .walk()
        .try_zip(view_of(numbers).window(0, count).expect("the count is at most six").walk())
        .match(
            [](auto pairs) noexcept {
                return static_cast<decltype(pairs)&&>(pairs)
                    .map([](auto const& pair) noexcept {
                        auto const& [first, second] = pair;
                        return *first * *second;
                    })
                    .fold(0, Sum{});
            },
            [](LengthMismatch) noexcept { return -1; });
}
static_assert(try_zip_dot(3) == 1 * 1 + 2 * 2 + 3 * 3);
static_assert(try_zip_dot(2) == -1);
static_assert(try_zip_dot(4) == -1);
static_assert(view_of(numbers).walk().try_zip(numbers.view().walk()).expect("one run").size() == 6);

// A zip of walks of values, and a limit and a skip of a zip.
[[nodiscard]] consteval int zip_of_values() {
    return left_numbers.view()
        .walk()
        .map(Twice{})
        .zip(right_numbers.view().walk().map(Twice{}))
        .skip(ElementCount{1})
        .map([](auto const& pair) noexcept {
            auto const& [first, second] = pair;
            return first - second;
        })
        .fold(0, Digits{});
}
static_assert(zip_of_values() == (4 - 10) * 10 + (6 - 8));

// flat_map walks the walk that the function gives for each element: the
// runs 1, 1 2, 1 2 3 and 1 2 3 4.
[[nodiscard]] consteval int flat_digits() {
    return view_of(lengths)
        .walk()
        .flat_map([](int const& length) noexcept {
            return lengths.view().window(0, static_cast<std::size_t>(length)).expect("a prefix fits").walk();
        })
        .fold(0, Digits{});
}
static_assert(flat_digits() == 1121231234);
static_assert(view_of(lengths)
                  .walk()
                  .flat_map([](int const& length) noexcept {
                      return lengths.view().window(0, static_cast<std::size_t>(length)).expect("a prefix fits").walk();
                  })
                  .count()
              == 10);
static_assert(view_of(lengths)
                  .walk()
                  .flat_map([](int const& length) noexcept {
                      return lengths.view().window(0, static_cast<std::size_t>(length)).expect("a prefix fits").walk();
                  })
                  .any_of([](int const& value) noexcept { return value == 4; }));

// The cursor loop over a filter with a limit, and over a map with a skip.
[[nodiscard]] consteval int cursor_digits() {
    int digits = 0;
    for (int const value : view_of(numbers).walk().filter(IsEven{}).limit(ElementCount{2})) {
        digits = digits * 10 + value;
    }
    for (int const value : view_of(numbers).walk().map(Twice{}).skip(ElementCount{4})) {
        digits = digits * 10 + value;
    }
    for (auto const [first, second] : left_numbers.view().walk().zip(right_numbers.view().walk())) {
        digits = digits * 10 + (*second - *first);
    }
    return digits;
}
// The digits: 2 and 4 of the filter, 10 and 12 of the map, and 5, 3 and 1 of
// the zip.
static_assert(cursor_digits() == 2512531);

// The element of a walk: a reference for a View, the result of fn for a
// map, and a Record of two Refs with the brand of the call for a zip.
using IntWalk = Walk<ViewStage<int const, dynamic_extent>>;
using FixedWalk = Walk<ViewStage<int const, 4>>;
static_assert(std::is_same_v<decltype(view_of(numbers).walk()), IntWalk>);
static_assert(std::is_same_v<decltype(std::declval<View<int const, 4>>().walk()), FixedWalk>);
static_assert(std::is_same_v<ViewStage<int const, 4>::element_reference, int const&>);
static_assert(std::is_same_v<MapStage<ViewStage<int const, 4>, Twice>::element_reference, int>);
struct ZipBrand final {};
static_assert(std::is_same_v<ZipStage<ViewStage<int const, 4>, ViewStage<int, 4>, ZipBrand>::element_reference,
                             Record<Ref<int const, ZipBrand>, Ref<int, ZipBrand>>>);
static_assert(
    std::is_same_v<ZipStage<MapStage<ViewStage<int const, 4>, Twice>, ViewStage<int, 4>, ZipBrand>::element_reference,
                   Record<int, Ref<int, ZipBrand>>>);
static_assert(ZipStage<ViewStage<int const, dynamic_extent>, ViewStage<int, 4>, ZipBrand>::extent == 4);

// A walk holds its run and no more: a stateless function adds no byte.
static_assert(sizeof(FixedWalk) == sizeof(void*));
static_assert(sizeof(IntWalk) == 2 * sizeof(void*));
static_assert(sizeof(Walk<MapStage<ViewStage<int const, 4>, Twice>>) == sizeof(void*));
static_assert(sizeof(Walk<FilterStage<ViewStage<int const, dynamic_extent>, IsEven>>) == 2 * sizeof(void*));

// limit and skip take an ElementCount, and no number converts to it.
template <class W>
constexpr bool limit_accepts_number = requires { std::declval<W>().limit(3); };
static_assert(!limit_accepts_number<IntWalk>);
static_assert(!std::is_convertible_v<std::size_t, ElementCount>);

// Each adapter and each terminal takes the walk through &&, so a named walk
// moves into its next step.
template <class W>
constexpr bool maps_named_walk = requires(W& walk) { walk.map(Twice{}); };
template <class W>
constexpr bool folds_named_walk = requires(W& walk) { walk.fold(0, Sum{}); };
static_assert(!maps_named_walk<IntWalk> && !folds_named_walk<IntWalk>);

// The function of each adapter and each terminal holds no linear value by
// value (LinearWalkFunction): the walk ends it with no consume.  A function
// that holds a token is refused, and a function that holds none is taken.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] WalkToken {
    int handle = 0;
    constexpr explicit WalkToken(int value) noexcept : handle{value} {}
    constexpr WalkToken(WalkToken&&) noexcept = default;
    constexpr WalkToken& operator=(WalkToken&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    WalkToken(WalkToken const&) = delete("a linear token of the check does not copy");
};
using MapsWithToken = decltype([held = WalkToken{1}](int const& value) noexcept { return value + held.handle; });
using KeepsWithToken = decltype([held = WalkToken{1}](int const& value) noexcept { return value > held.handle; });
using FoldsWithToken =
    decltype([held = WalkToken{1}](int sum, int const& value) noexcept { return sum + value + held.handle; });
template <class W, class Fn>
constexpr bool maps_with = requires { std::declval<W>().map(std::declval<Fn>()); };
template <class W, class Fn>
constexpr bool filters_with = requires { std::declval<W>().filter(std::declval<Fn>()); };
template <class W, class Fn>
constexpr bool folds_with = requires { std::declval<W>().fold(0, std::declval<Fn>()); };
template <class W, class Fn>
constexpr bool tests_any_with = requires { std::declval<W>().any_of(std::declval<Fn>()); };
template <class W, class Fn>
constexpr bool tests_all_with = requires { std::declval<W>().all_of(std::declval<Fn>()); };
static_assert(!maps_with<IntWalk, MapsWithToken> && maps_with<IntWalk, Twice>);
static_assert(!filters_with<IntWalk, KeepsWithToken> && filters_with<IntWalk, IsEven>);
static_assert(!folds_with<IntWalk, FoldsWithToken> && folds_with<IntWalk, Sum>);
static_assert(!tests_any_with<IntWalk, KeepsWithToken> && tests_any_with<IntWalk, IsEven>);
static_assert(!tests_all_with<IntWalk, KeepsWithToken> && tests_all_with<IntWalk, IsEven>);

// An exact walk gives size() and no count().  A walk with a filter gives
// count() and no size(), and it has no skip, zip or try_zip.
using FilterWalk = Walk<FilterStage<ViewStage<int const, dynamic_extent>, IsEven>>;
template <class W>
constexpr bool has_size = requires(W const& walk) { walk.size(); };
template <class W>
constexpr bool has_count = requires { std::declval<W>().count(); };
template <class W>
constexpr bool has_skip = requires { std::declval<W>().skip(ElementCount{1}); };
static_assert(has_size<IntWalk> && !has_count<IntWalk>);
static_assert(!has_size<FilterWalk> && has_count<FilterWalk> && !has_skip<FilterWalk> && has_skip<IntWalk>);

// zip takes two exact walks of one fixed extent, and try_zip two exact walks
// when one of them has a dynamic extent.  Each other pairing is refused.
template <class First, class Second>
constexpr bool zips = requires { std::declval<First>().zip(std::declval<Second>()); };
template <class First, class Second>
constexpr bool try_zips = requires { std::declval<First>().try_zip(std::declval<Second>()); };
using OtherFixedWalk = Walk<ViewStage<int const, 5>>;
static_assert(zips<FixedWalk, FixedWalk> && !try_zips<FixedWalk, FixedWalk>);
static_assert(!zips<FixedWalk, OtherFixedWalk> && !try_zips<FixedWalk, OtherFixedWalk>);
static_assert(!zips<IntWalk, FixedWalk> && try_zips<IntWalk, FixedWalk> && try_zips<IntWalk, IntWalk>);
static_assert(!zips<FilterWalk, IntWalk> && !try_zips<FilterWalk, IntWalk> && !try_zips<IntWalk, FilterWalk>);

// A function that can throw, or a test that gives no bool, is refused.
constexpr auto throwing_test = [](int const&) { return true; };
constexpr auto counting_test = [](int const& value) noexcept { return value; };
template <class W, class Test>
constexpr bool filters = requires(Test test) { std::declval<W>().filter(test); };
template <class W, class Test>
constexpr bool tests_any = requires(Test test) { std::declval<W>().any_of(test); };
static_assert(filters<IntWalk, IsEven> && tests_any<IntWalk, IsEven>);
static_assert(!filters<IntWalk, decltype(throwing_test)> && !tests_any<IntWalk, decltype(throwing_test)>);
static_assert(!filters<IntWalk, decltype(counting_test)> && !tests_any<IntWalk, decltype(counting_test)>);
template <class W, class Fn>
constexpr bool expands = requires(Fn fn) { std::declval<W>().flat_map(fn); };
static_assert(!expands<IntWalk, Twice>);

// The cursor of a temporary walk of a map, a filter or a zip is refused: it
// would point into an object that the full expression ends.  A cursor of a
// root stage holds only places of the run.  A flat_map walk has no cursor.
using MapWalk = Walk<MapStage<ViewStage<int const, 4>, Twice>>;
using FlatWalk = decltype(view_of(lengths).walk().flat_map(
    [](int const&) noexcept { return lengths.view().window(0, 1).expect("one element fits").walk(); }));
template <class W>
constexpr bool begins_temporary = requires { std::declval<W>().begin(); };
template <class W>
constexpr bool begins_named = requires(W const& walk) { walk.begin(); };
static_assert(begins_named<IntWalk> && begins_temporary<IntWalk>);
static_assert(begins_named<FilterWalk> && !begins_temporary<FilterWalk>);
static_assert(begins_named<MapWalk> && !begins_temporary<MapWalk>);
static_assert(!begins_named<FlatWalk> && !begins_temporary<FlatWalk>);

// The iteration verbs of a View give a walk.  A limit or a skip of a walk of
// enumerate() keeps the index of each element in the View.
[[nodiscard]] consteval int verb_digits() {
    int digits = 0;
    for (int const value : view_of(left_numbers).reverse()) {
        digits = digits * 10 + value;
    }
    digits = digits * 10 + view_of(numbers).reverse().skip(ElementCount{4}).fold(0, Digits{});
    digits = digits * 10
           + view_of(numbers)
                 .windows<4>()
                 .map([](View<int const, 4> run) noexcept { return run.at(index<3>()); })
                 .fold(0, Digits{});
    return digits;
}
static_assert(verb_digits() == ((321 * 10 + 21) * 10 + 456));
static_assert(view_of(numbers).windows<4>().size() == 3 && view_of(numbers).windows<9>().size() == 0);
static_assert(view_of(numbers)
                  .windows<4>()
                  .limit(ElementCount{2})
                  .fold(0, [](int sum, View<int const, 4> run) noexcept { return sum + run.at(index<0>()); })
              == 1 + 2);
static_assert(view_of(numbers).chunks<4>().size() == 1 && view_of(numbers).chunks<4>().rest().size() == 2);
static_assert(view_of(numbers).chunks<2>().limit(ElementCount{1}).rest().size() == 4);
static_assert(view_of(numbers).chunks<2>().skip(ElementCount{1}).rest().size() == 0);
static_assert(view_of(numbers).chunks<2>().skip(ElementCount{1}).fold(0, [](int sum, View<int const, 2> run) noexcept {
    return sum * 10 + run.at(index<1>());
}) == 46);
static_assert(numbers.view()
                  .enumerate()
                  .skip(ElementCount{2})
                  .limit(ElementCount{2})
                  .fold(std::size_t{0},
                        [](std::size_t sum, auto const& pair) noexcept {
                            auto const& [position, element] = pair;
                            return sum * 100 + position.raw() * 10 + static_cast<std::size_t>(*element);
                        })
              == 23 * 100 + 34);
[[nodiscard]] consteval std::size_t enumerate_cursor() {
    std::size_t positions = 0;
    for (auto const [position, element] : numbers.view().enumerate().skip(ElementCount{4})) {
        positions = positions * 10 + position.raw() + static_cast<std::size_t>(*element);
    }
    return positions;
}
static_assert(enumerate_cursor() == 9 * 10 + 11);

// The closed surface: the shared verbs and the names of the family list.
// rest is the View after the last run of a chunk walk.
inline constexpr TextView walk_names{"map filter limit skip zip try_zip flat_map fold count any_of all_of rest"};
CRUCIBLE_CLOSED_SURFACE(walk_names, Walk<ReverseStage<int>>);
CRUCIBLE_CLOSED_SURFACE(walk_names, Walk<WindowStage<int const, 2>>);
CRUCIBLE_CLOSED_SURFACE(walk_names, Walk<ChunkStage<int, 3>>);
CRUCIBLE_CLOSED_SURFACE(walk_names, Walk<EnumerateStage<int, 4, ZipBrand, 4>>);
CRUCIBLE_CLOSED_SURFACE(walk_names, IntWalk);
CRUCIBLE_CLOSED_SURFACE(walk_names, FixedWalk);
CRUCIBLE_CLOSED_SURFACE(walk_names, FilterWalk);
CRUCIBLE_CLOSED_SURFACE(walk_names, Walk<MapStage<ViewStage<int const, 4>, Twice>>);
CRUCIBLE_CLOSED_SURFACE(walk_names, Walk<LimitStage<FilterStage<ViewStage<int const, dynamic_extent>, IsEven>>>);
CRUCIBLE_CLOSED_SURFACE(walk_names, Walk<ZipStage<ViewStage<int const, 4>, ViewStage<int, 4>, ZipBrand>>);
CRUCIBLE_CLOSED_SURFACE(walk_names, LengthMismatch);
CRUCIBLE_CLOSED_SURFACE(walk_names, MapStage<ViewStage<int const, 4>, Twice>::Cursor, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(walk_names, FilterStage<ViewStage<int const, 4>, IsEven>::Cursor, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(walk_names, LimitStage<FilterStage<ViewStage<int const, 4>, IsEven>>::Cursor, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(walk_names, ZipStage<ViewStage<int const, 4>, ViewStage<int, 4>, ZipBrand>::Cursor,
                        CursorProtocol);

}  // namespace detail::walk_checks

}  // namespace foundation::core
