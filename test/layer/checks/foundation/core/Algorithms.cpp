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

// equal takes two Views of one Equatable element type: a leaf, or a class
// with an == that gives a bool and does not throw.  A float compares by its
// bits, so a run of floats takes one memcmp.
template <class Left, class Right>
constexpr bool equal_accepts = requires(Left left, Right right) { equal(left, right); };
static_assert(equal_accepts<View<int>, View<int const>>);
static_assert(equal_accepts<View<int const, 4>, View<int>>);
static_assert(!equal_accepts<View<int>, View<long>>);
static_assert(!equal_accepts<View<ThrowingCompare>, View<ThrowingCompare>>);
static_assert(BytewiseEqualElement<int> && BytewiseEqualElement<Shade> && BytewiseEqualElement<std::byte>);
static_assert(BytewiseEqualElement<float> && BytewiseEqualElement<double> && !BytewiseEqualElement<Record>);

// The float rule over a View: -0 and +0 differ, and the canonical NaN equals
// itself.
constexpr FixedArray<double, 1> positive_zero{0.0};
constexpr FixedArray<double, 1> negative_zero{-0.0};
constexpr FixedArray<double, 1> canonical_nan{quiet_nan_of<double>};
static_assert(!equal(positive_zero.view(), negative_zero.view()));
static_assert(equal(canonical_nan.view(), canonical_nan.view()));

// order is lexicographic, and a prefix orders first.  A float orders by the
// totalOrder of IEEE 754, so -0 orders before +0.
constexpr FixedArray<unsigned char, 3> low_bytes{1, 2, 3};
constexpr FixedArray<unsigned char, 3> high_bytes{1, 2, 4};
static_assert(std::is_lt(order(low_bytes.view(), high_bytes.view())));
static_assert(std::is_lt(order(low_bytes.view().window(0, 2).expect("two bytes fit"), low_bytes.view())));
static_assert(std::is_eq(order(low_bytes.view(), low_bytes.view())));
static_assert(std::is_lt(order(negative_zero.view(), positive_zero.view())));
static_assert(std::is_lt(order(view_of(numbers), view_of(other_numbers))));

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

// A predicate that can throw, or that gives no bool, is refused.
constexpr auto throwing_predicate = [](int) { return true; };
constexpr auto counting_predicate = [](int value) noexcept { return value; };
template <class Predicate>
constexpr bool scan_accepts = requires(View<int const> view, Predicate predicate) { find_if(view, predicate); };
static_assert(scan_accepts<decltype(is_even)>);
static_assert(!scan_accepts<decltype(throwing_predicate)>);
static_assert(!scan_accepts<decltype(counting_predicate)>);

// The prefix and the suffix of a text.
static_assert(starts_with(TextView{"cpu12"}, TextView{"cpu"}) && !starts_with(TextView{"cp"}, TextView{"cpu"}));
static_assert(starts_with(TextView{"cpu"}, TextView{}) && starts_with(TextView{}, TextView{}));
static_assert(ends_with(TextView{"node3/cpulist"}, TextView{"cpulist"})
              && !ends_with(TextView{"list"}, TextView{"cpulist"}));
static_assert(!ends_with(TextView{"cpulisx"}, TextView{"list"}));

// is_window_of takes the two texts in their roles, so the swapped call and a
// call with two plain texts do not compile.
template <class First, class Second>
constexpr bool window_accepts = requires(First first, Second second) { is_window_of(first, second); };
static_assert(window_accepts<WindowPartText, WindowWholeText>);
static_assert(!window_accepts<WindowWholeText, WindowPartText>);
static_assert(!window_accepts<TextView, TextView>);
static_assert(!window_accepts<WindowPartText, TextView>);
static_assert(std::is_same_v<decltype(is_window_of(WindowPartText{TextView{}}, WindowWholeText{TextView{}})), bool>);

// The texts of the window checks are two buffers of internal linkage with the
// same characters, so each buffer is an object of its own.
constexpr FixedText<8> window_letters{"abcdefgh"};
constexpr FixedText<8> other_letters{"abcdefgh"};

// A part of the letters against the whole of the characters from 2 to 6.
[[nodiscard]] consteval bool is_letters_window(std::size_t offset, std::size_t count) noexcept {
    TextView const part = window_letters.view().window(offset, count).expect("the part fits the letters");
    TextView const whole = window_letters.view().window(2, 4).expect("the whole fits the letters");
    return is_window_of(WindowPartText{part}, WindowWholeText{whole});
}

// A part lies inside when it starts at or after the whole and ends at or
// before its end, at each edge.  A part that starts before the whole or ends
// after it is no window.
static_assert(is_letters_window(2, 4) && is_letters_window(3, 2));
static_assert(is_letters_window(2, 1) && is_letters_window(5, 1));
static_assert(!is_letters_window(1, 3) && !is_letters_window(3, 4) && !is_letters_window(0, 8));

// An empty part is a window from the start of the whole to its end.
static_assert(is_letters_window(2, 0) && is_letters_window(4, 0) && is_letters_window(6, 0));
static_assert(!is_letters_window(1, 0) && !is_letters_window(7, 0));

// The query reads the addresses: a part of the second buffer with the same
// characters is no window, and the empty text with no address is a window
// only of a text with no address.
[[nodiscard]] consteval bool is_window_of_letters(TextView part) noexcept {
    return is_window_of(WindowPartText{part},
                        WindowWholeText{window_letters.view().window(2, 4).expect("the whole fits the letters")});
}
static_assert(!is_window_of_letters(other_letters.view().window(2, 4).expect("the part fits the letters")));
static_assert(!is_window_of_letters(TextView{}));
static_assert(is_window_of(WindowPartText{TextView{}}, WindowWholeText{TextView{}}));

// The lower bound of a key in a sorted run.
constexpr auto identity_key = [](int value) noexcept { return value; };
static_assert(find_lower_bound_by_key(view_of(numbers), identity_key, 15) == 2);
static_assert(find_lower_bound_by_key(view_of(numbers), identity_key, 14) == 2);
static_assert(find_lower_bound_by_key(view_of(numbers), identity_key, 0) == 0);
static_assert(find_lower_bound_by_key(view_of(numbers), identity_key, 100) == 6);
static_assert(find_lower_bound_by_key(View<int const>{}, identity_key, 1) == 0);
constexpr auto float_key = [](int value) noexcept { return static_cast<float>(value); };
template <class Key>
constexpr bool bound_accepts = requires(View<int const> view, Key key) { find_lower_bound_by_key(view, key, 1); };
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

// The predicate of find_if and the key function of find_lower_bound_by_key
// hold no linear value by value: the door calls them many times, and a call
// is no consume.  Each binds to the deleted twin of its door when it holds a
// token (LinearElementFunction).
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] SearchToken {
    int handle = 0;
    constexpr explicit SearchToken(int value) noexcept : handle{value} {}
    constexpr SearchToken(SearchToken&&) noexcept = default;
    constexpr SearchToken& operator=(SearchToken&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    SearchToken(SearchToken const&) = delete("a linear token of the check does not copy");
};
using KeysWithToken = decltype([held = SearchToken{1}](int const& value) noexcept { return value + held.handle; });
using KeysWithNoToken = decltype([](int const& value) noexcept { return value; });
template <class Key>
constexpr bool searches_with =
    requires(View<int const> elements, Key key) { find_lower_bound_by_key(elements, static_cast<Key&&>(key), 0); };
static_assert(searches_with<KeysWithNoToken> && !searches_with<KeysWithToken>);
static_assert(LinearElementFunction<KeysWithToken, int> && !KeyOf<KeysWithToken, int>);

// The search refuses a key function that can store a borrow of an element
// outside the search, and the deleted twin gives the reason.
template <class Kept>
struct KeyThatKeeps final {
    Kept& kept;
    constexpr int operator()(int const& value) const noexcept { return value; }
};
static_assert(!KeyOf<KeyThatKeeps<Option<View<int const>>>, int> && KeyOf<KeyThatKeeps<int>, int>);
static_assert(StoringElementFunction<KeyThatKeeps<Option<View<int const>>>, int>);
static_assert(!searches_with<KeyThatKeeps<Option<View<int const>>>> && searches_with<KeyThatKeeps<int>>);

}  // namespace detail::algorithm_checks

}  // namespace foundation::core
