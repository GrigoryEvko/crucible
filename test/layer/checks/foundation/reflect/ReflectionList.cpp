// The compile-time checks of foundation/reflect/ReflectionList.h.

#include <foundation/reflect/ReflectionList.h>

#include <type_traits>

namespace foundation::reflect {

namespace detail::reflection_list_self_test {

enum class Kind : unsigned char {
    first = 1,
    second = 2,
    third = 4
};
enum class Empty : unsigned char {
};

inline constexpr ReflectionList kKinds = reflection_list(std::meta::enumerators_of(^^Kind));
inline constexpr ReflectionList kNone = reflection_list(std::meta::enumerators_of(^^Empty));

static_assert(kKinds.size() == 3 && !kKinds.is_empty());
static_assert(kNone.size() == 0 && kNone.is_empty());
static_assert(kKinds[0] == ^^Kind::first);
static_assert(kKinds[2] == ^^Kind::third);

// The list with an owner is the list of the same range: the same count and
// the same reflections in the same order.  The owner changes no element.
inline constexpr ReflectionList kOwnedKinds = reflection_list<^^Empty>(std::meta::enumerators_of(^^Kind));
static_assert(kOwnedKinds.size() == 3);
static_assert(kOwnedKinds[0] == ^^Kind::first);
static_assert(kOwnedKinds[2] == ^^Kind::third);
static_assert(kOwnedKinds[1] == kKinds[1]);
static_assert(reflection_list<^^Kind>(std::meta::enumerators_of(^^Empty)).is_empty());

// In a template, a query of a fixed entity takes a template parameter as its
// owner.  Each instantiation gives the same list.
template <class Owner>
consteval std::size_t count_of_fixed_kinds() {
    static constexpr ReflectionList kinds = reflection_list<^^Owner>(std::meta::enumerators_of(^^Kind));
    return kinds.size();
}
static_assert(count_of_fixed_kinds<int>() == 3 && count_of_fixed_kinds<Empty>() == 3);

// A read below the count is a constant expression, and a read at the count
// is not one.
template <std::size_t Index>
concept readable_at = requires { typename std::integral_constant<std::meta::info, kKinds[Index]>; };
static_assert(readable_at<2>);
static_assert(!readable_at<3>, "a read at the count of a list is no constant expression");

// The walk of an expansion statement gives each reflection one time.
consteval unsigned sum_of_kinds() {
    unsigned total = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info kind : kKinds) { total += static_cast<unsigned>([:kind:]); }
#pragma GCC diagnostic pop
    return total;
}
static_assert(sum_of_kinds() == 7);

// The walk of an empty list runs no step.
consteval unsigned count_of_none() {
    unsigned total = 0;
    template for (constexpr std::meta::info kind : kNone) {
        static_cast<void>(kind);
        ++total;
    }
    return total;
}
static_assert(count_of_none() == 0);

// The order of the walk is the order of the range.
consteval bool walks_in_order() {
    unsigned last = 0;
    bool is_ordered = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info kind : kKinds) {
        unsigned const value = static_cast<unsigned>([:kind:]);
        is_ordered = is_ordered && value > last;
        last = value;
    }
#pragma GCC diagnostic pop
    return is_ordered;
}
static_assert(walks_in_order());

// No constructor of the list is public, so reflection_list is its one door.
static_assert(!std::is_constructible_v<ReflectionList, std::meta::info const*, std::size_t>);

// The copies are user-provided, so no std::bit_cast and no lifetime start
// over bytes builds a list or a cursor.
static_assert(!std::is_trivially_copyable_v<ReflectionList> && !std::is_implicit_lifetime_v<ReflectionList>);
static_assert(!std::is_trivially_copyable_v<ReflectionCursor> && !std::is_implicit_lifetime_v<ReflectionCursor>);

}  // namespace detail::reflection_list_self_test

}  // namespace foundation::reflect
