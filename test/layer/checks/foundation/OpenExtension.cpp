// The compile-time checks of foundation/OpenExtension.h.

#include <foundation/OpenExtension.h>

#include <type_traits>

namespace foundation {

namespace open_extension_checks {

// A planted class template and a planted variable template with the open
// mark, and a planted class template with no mark.
template <class T>
struct[[= open_extension{"the check of the header plants an open trait"}]] open_trait {};

template <class T>
[[= open_extension{"the check of the header plants an open flag"}]] inline constexpr bool open_flag = false;

template <class T>
struct closed_trait {};

// Reflection reads the mark of the template from each instance.
static_assert(is_open_extension(^^open_trait<int>));
static_assert(is_open_extension(^^open_flag<int>));
static_assert(!is_open_extension(^^closed_trait<int>));

// The mark keeps its reason, with the final zero.
constexpr open_extension kMark{"a reason"};
static_assert(kMark.reason[0] == 'a' && kMark.reason[7] == 'n' && kMark.reason[8] == '\0');

// A mark has a reason of at least one letter, and the reason fits the mark.
static_assert(std::is_constructible_v<open_extension, char const (&)[2]>);
static_assert(!std::is_constructible_v<open_extension, char const (&)[1]>);
static_assert(std::is_constructible_v<open_extension, char const (&)[open_extension_reason_size]>);
static_assert(!std::is_constructible_v<open_extension, char const (&)[open_extension_reason_size + 1]>);

}  // namespace open_extension_checks

}  // namespace foundation
