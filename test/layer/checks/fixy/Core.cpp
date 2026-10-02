// The compile-time checks of fixy/Core.h.

#include <fixy/Core.h>

#include <type_traits>

namespace fixy::detail::core_checks {

// Each fixy name is the foundation entity, not a second type.
static_assert(std::is_same_v<::fixy::Option<int>, ::foundation::core::Option<int>>);
static_assert(std::is_same_v<::fixy::NoValue, ::foundation::core::NoValue>);
static_assert(std::is_same_v<decltype(::fixy::none), decltype(::foundation::core::none)>);
static_assert(::fixy::Option<int>{::fixy::none}.is_none());
static_assert(std::is_same_v<::fixy::Box<int>, ::foundation::core::Box<int>>);
static_assert(std::is_same_v<::fixy::Atomic<int>, ::foundation::core::Atomic<int>>);
static_assert(std::is_same_v<::fixy::CacheLine<int>, ::foundation::core::CacheLine<int>>);
static_assert(std::is_same_v<::fixy::CasOutcome<int>, ::foundation::core::CasOutcome<int>>);
static_assert(std::is_same_v<::fixy::Tally, ::foundation::core::Tally>);
static_assert(std::is_same_v<::fixy::View<int, 4>, ::foundation::core::View<int, 4>>);
static_assert(::fixy::dynamic_extent == ::foundation::core::dynamic_extent);
static_assert(std::is_same_v<::fixy::Site, ::foundation::core::Site>);
static_assert(std::is_same_v<::fixy::Fmt<>, ::foundation::core::Fmt<>>);
static_assert(noexcept(::fixy::fatal("a text of a report")) && noexcept(::fixy::unreachable()));

}  // namespace fixy::detail::core_checks
