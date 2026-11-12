// The compile-time checks of foundation/core/Algorithms.h.

#include <foundation/core/Algorithms.h>

#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::algorithm_checks {

struct Record {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;

    [[nodiscard]] friend constexpr bool operator==(Record const&, Record const&) noexcept = default;
};

// A record with a padding byte after its first member.
struct Padded {
    std::uint8_t tag = 0;
    std::uint32_t value = 0;
};

// An == that can throw.
struct ThrowingCompare {
    int value = 0;
    [[nodiscard]] friend bool operator==(ThrowingCompare, ThrowingCompare) { return true; }
};

enum class Shade : std::uint8_t {
    dark,
    light
};

// The runs of the checks: constant arrays of internal linkage.
constexpr FixedArray<int, 6> numbers{4, 8, 15, 16, 23, 42};
constexpr FixedArray<int, 6> same_numbers{4, 8, 15, 16, 23, 42};
constexpr FixedArray<int, 6> other_numbers{4, 8, 15, 16, 23, 43};
constexpr FixedArray<Record, 2> records{Record{1, 2, 3}, Record{4, 5, 6}};
constexpr FixedArray<Record, 2> same_records{Record{1, 2, 3}, Record{4, 5, 6}};

// The View of a dynamic extent of a run.
[[nodiscard]] consteval View<int const> view_of(FixedArray<int, 6> const& run) noexcept { return run.view(); }

[[nodiscard]] consteval View<Record const> records_of(FixedArray<Record, 2> const& run) noexcept { return run.view(); }

// equal compares the counts and then each element.
static_assert(equal(view_of(numbers), view_of(same_numbers)));
static_assert(!equal(view_of(numbers), view_of(other_numbers)));
static_assert(!equal(view_of(numbers), view_of(numbers).window(0, 5).expect("five numbers fit")));
static_assert(equal(View<int const>{}, view_of(numbers).window(6, 0).expect("the empty window fits")));
static_assert(equal(records_of(records), records_of(same_records)));
static_assert(equal(numbers.view(), view_of(same_numbers)));
static_assert(equal(TextView{"queue"}, TextView{"queue"}));
static_assert(!equal(TextView{"queue"}, TextView{"quell"}));

// equal takes two Views of one element type, with an == that gives a bool
// and does not throw.
template <class Left, class Right>
constexpr bool equal_accepts = requires(Left left, Right right) { equal(left, right); };
static_assert(equal_accepts<View<int>, View<int const>>);
static_assert(equal_accepts<View<int const, 4>, View<int>>);
static_assert(!equal_accepts<View<int>, View<long>>);
static_assert(!equal_accepts<View<ThrowingCompare>, View<ThrowingCompare>>);
static_assert(BytewiseEqualElement<int> && BytewiseEqualElement<Shade> && BytewiseEqualElement<std::byte>);
static_assert(!BytewiseEqualElement<float> && !BytewiseEqualElement<Record>);

// find gives the first position, or no value.
static_assert(find(view_of(numbers), 15).expect("15 is in the run") == 2);
static_assert(find(view_of(numbers), 4).expect("4 is in the run") == 0);
static_assert(find(view_of(numbers), 7).is_none());
static_assert(find(View<int const>{}, 7).is_none());
static_assert(find(TextView{"a/b/c"}, '/').expect("the text holds a slash") == 1);
static_assert(find(TextView{"abc"}, '/').is_none());
static_assert(find(TextView{"key=value"}, TextView{"=val"}).expect("the text holds the needle") == 3);
static_assert(find(TextView{"aaab"}, TextView{"aab"}).expect("the needle starts after one character") == 1);
static_assert(find(TextView{"abc"}, TextView{}).expect("an empty needle is at the start") == 0);
static_assert(find(TextView{"ab"}, TextView{"abc"}).is_none());
static_assert(find(TextView{}, TextView{"a"}).is_none());

// The scans with a predicate.
constexpr auto is_even = [](int value) noexcept { return value % 2 == 0; };
constexpr auto is_large = [](int value) noexcept { return value > 40; };
constexpr auto is_negative = [](int value) noexcept { return value < 0; };
static_assert(find_if(view_of(numbers), is_large).expect("42 is large") == 5);
static_assert(find_if(view_of(numbers), is_negative).is_none());
static_assert(count_if(view_of(numbers), is_even) == 4);
static_assert(any_of(view_of(numbers), is_large) && !any_of(view_of(numbers), is_negative));
static_assert(!all_of(view_of(numbers), is_even) && all_of(View<int const>{}, is_negative));
static_assert(!any_of(View<int const>{}, is_even));

// A predicate that can throw, or that gives no bool, is refused.
constexpr auto throwing_predicate = [](int) { return true; };
constexpr auto counting_predicate = [](int value) noexcept { return value; };
template <class Predicate>
constexpr bool scan_accepts = requires(View<int const> view, Predicate predicate) { count_if(view, predicate); };
static_assert(scan_accepts<decltype(is_even)>);
static_assert(!scan_accepts<decltype(throwing_predicate)>);
static_assert(!scan_accepts<decltype(counting_predicate)>);

// The prefix and the suffix of a text.
static_assert(starts_with(TextView{"cpu12"}, TextView{"cpu"}) && !starts_with(TextView{"cp"}, TextView{"cpu"}));
static_assert(starts_with(TextView{"cpu"}, TextView{}) && starts_with(TextView{}, TextView{}));
static_assert(ends_with(TextView{"node3/cpulist"}, TextView{"cpulist"})
              && !ends_with(TextView{"list"}, TextView{"cpulist"}));
static_assert(!ends_with(TextView{"cpulisx"}, TextView{"list"}));

// The lower bound of a key in a sorted run.
constexpr auto identity_key = [](int value) noexcept { return value; };
static_assert(lower_bound_by_key(view_of(numbers), identity_key, 15) == 2);
static_assert(lower_bound_by_key(view_of(numbers), identity_key, 14) == 2);
static_assert(lower_bound_by_key(view_of(numbers), identity_key, 0) == 0);
static_assert(lower_bound_by_key(view_of(numbers), identity_key, 100) == 6);
static_assert(lower_bound_by_key(View<int const>{}, identity_key, 1) == 0);
constexpr auto float_key = [](int value) noexcept { return static_cast<float>(value); };
template <class Key>
constexpr bool bound_accepts = requires(View<int const> view, Key key) { lower_bound_by_key(view, key, 1); };
static_assert(bound_accepts<decltype(identity_key)>);
static_assert(!bound_accepts<decltype(float_key)>);

// bytes takes only a plain object with no padding bit.
static_assert(ByteImageable<Record> && ByteImageable<std::uint32_t> && ByteImageable<Shade>);
static_assert(!ByteImageable<Padded>);
static_assert(!ByteImageable<int const*>);
static_assert(!ByteImageable<View<int>>);
static_assert(std::is_same_v<decltype(bytes(std::declval<Record const&>())), View<std::byte const, sizeof(Record)>>);
template <class T>
constexpr bool bytes_of_temporary = requires { bytes(std::declval<T>()); };
static_assert(!bytes_of_temporary<Record>);

}  // namespace detail::algorithm_checks

}  // namespace foundation::core
