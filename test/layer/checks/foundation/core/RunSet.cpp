// The compile-time checks of foundation/core/RunSet.h.

#include <foundation/core/RunSet.h>

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::run_set_checks {

struct Wide {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
};

// Two runs at slot 0 and one run at slot 1.
using Mixed = RunSet<std::uint16_t, RunOf<std::uint32_t, 0>, RunOf<Wide, 0>, RunOf<std::uint64_t, 1>>;

// The shape of the arrays of one traced op: three runs of the inputs, two
// runs of the outputs and one run of the scalars.
using SixRuns = RunSet<std::uint16_t, RunOf<std::uint32_t, 1>, RunOf<std::uint32_t, 0>, RunOf<std::uint32_t, 0>,
                       RunOf<Wide, 0>, RunOf<Wide, 1>, RunOf<std::int64_t, 2>>;

// The table of counts holds one count for each slot, and the pointers come
// first.  So six runs with three counts of two bytes hold 54 bytes.
static_assert(Mixed::slot_count == 2 && Mixed::run_count == 3);
static_assert(SixRuns::slot_count == 3 && SixRuns::run_count == 6);
static_assert(sizeof(Mixed) == 3 * sizeof(void*) + 8 && alignof(Mixed) == alignof(void*));
static_assert(sizeof(SixRuns) == 56);
static_assert(Mixed::slot_of<0> == 0 && Mixed::slot_of<1> == 0 && Mixed::slot_of<2> == 1);
static_assert(std::is_same_v<Mixed::element_t<1>, Wide>);

// A set copies, and it ends with no work.  The seal refuses a byte copy, so
// the set is not trivially copyable, as a View is not.
static_assert(std::is_nothrow_default_constructible_v<Mixed> && std::is_trivially_destructible_v<Mixed>);
static_assert(std::is_nothrow_copy_constructible_v<Mixed> && std::is_nothrow_copy_assignable_v<Mixed>);
static_assert(!std::is_trivially_copyable_v<Mixed>);

// A run takes an element that a View takes, with no const.  A set takes an
// unsigned count and runs only.
template <class T>
concept CanRun = requires { typename RunOf<T, 0>; };
static_assert(CanRun<Wide> && !CanRun<Wide const> && !CanRun<int[2]> && !CanRun<int&>);
template <class Count, class... Runs>
concept CanSet = requires { typename RunSet<Count, Runs...>; };
static_assert(CanSet<std::uint16_t, RunOf<int, 0>> && CanSet<std::size_t, RunOf<int, 2>>);
static_assert(!CanSet<int, RunOf<int, 0>> && !CanSet<bool, RunOf<int, 0>> && !CanSet<std::uint16_t, int>);
static_assert(!CanSet<std::uint16_t>);

// view<I>() gives a View of the elements, and a const set gives const
// elements.  The View of a temporary set is refused.
static_assert(std::is_same_v<decltype(std::declval<Mixed&>().view<1>()), View<Wide>>);
static_assert(std::is_same_v<decltype(std::declval<Mixed const&>().view<1>()), View<Wide const>>);
template <class Set>
concept ViewsFirst = requires { std::declval<Set>().template view<0>(); };
static_assert(ViewsFirst<Mixed&> && ViewsFirst<Mixed const&>);
static_assert(!ViewsFirst<Mixed&&> && !ViewsFirst<Mixed const&&>);

// An index past the runs and a slot past the table do not compile.
template <class Set>
concept ViewsFourth = requires { std::declval<Set&>().template view<3>(); };
template <class Set>
concept CountsThird = requires { std::declval<Set&>().template count<2>(); };
static_assert(!ViewsFourth<Mixed> && !CountsThird<Mixed>);

// The three states of a run: no elements at a count above zero, a count of
// zero, and the elements of the count.  A new count drops the runs of its
// slot only.
[[nodiscard]] consteval bool states_of_a_run() noexcept {
    FixedArray<std::uint32_t, 3> numbers{1, 2, 3};
    FixedArray<std::uint64_t, 2> wide_numbers{7, 8};
    Mixed set{};
    bool const starts_empty = set.has_run<0>() && set.view<0>().size() == 0 && set.count<0>() == 0;
    set.set_count<0>(3);
    set.set_count<1>(2);
    bool const absent = !set.has_run<0>() && set.view<0>().size() == 0 && set.count<0>() == 3;
    set.set<0>(numbers.view());
    set.set<2>(wide_numbers.view());
    bool const full = set.has_run<0>() && set.view<0>().size() == 3 && !set.has_run<1>() && set.has_run<2>();
    set.set_count<0>(2);
    bool const dropped = !set.has_run<0>() && set.view<0>().size() == 0 && set.view<2>().size() == 2;
    set.set_count<0>(0);
    bool const zero = set.has_run<0>() && set.has_run<1>() && set.view<1>().size() == 0;
    return starts_empty && absent && full && dropped && zero;
}
static_assert(states_of_a_run());

}  // namespace detail::run_set_checks

}  // namespace foundation::core
