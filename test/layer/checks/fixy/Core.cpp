// The compile-time checks of fixy/Core.h.

#include <fixy/Core.h>

#include <type_traits>

namespace fixy::detail::core_checks {

// Each fixy name is the foundation entity, not a second type.
static_assert(std::is_same_v<::fixy::Option<int>, ::foundation::core::Option<int>>);
static_assert(std::is_same_v<::fixy::NoValue, ::foundation::core::NoValue>);
static_assert(std::is_same_v<decltype(::fixy::none), decltype(::foundation::core::none)>);
static_assert(::fixy::Option<int>{::fixy::none}.is_none());

}  // namespace fixy::detail::core_checks
