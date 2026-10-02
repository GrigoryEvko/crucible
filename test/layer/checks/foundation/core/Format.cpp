// The compile-time checks of foundation/core/Format.h.

#include <foundation/core/Format.h>

#include <type_traits>

namespace foundation::core {

namespace detail::format_checks {

// format() gives the view of its text or the error Truncated, and the
// caller cannot drop the Result.
static_assert(
    std::is_same_v<decltype(format(std::declval<FixedText<8>&>(), Fmt<int>{"{}"}, 1)), Result<TextView, Truncated>>);
static_assert(noexcept(format(std::declval<FixedText<8>&>(), Fmt<>{"text"})));
static_assert(ErrorValue<Truncated>);

// The fixture neg_core_format_temporary_text shows that a temporary
// FixedText is refused.

}  // namespace detail::format_checks

}  // namespace foundation::core
