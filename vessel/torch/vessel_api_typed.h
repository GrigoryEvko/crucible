#pragma once

// Typed helpers for the internal Vessel C-ABI handle boundary.
//
// CrucibleHandle remains `void*` in vessel_api.h because it is exported
// to C / ctypes callers.  Inside C++, the handle is immediately tagged
// as source::ABIBoundary so foreign-runtime provenance is visible in the
// type system before the pointer reaches Vigil code.

#include "vessel_api.h"

#include <crucible/Platform.h>
#include <crucible/Vigil.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <type_traits>

namespace crucible::vessel {

using TypedHandle = safety::Tagged<Vigil*, safety::source::ABIBoundary>;

static_assert(sizeof(TypedHandle) == sizeof(CrucibleHandle));
static_assert(alignof(TypedHandle) == alignof(CrucibleHandle));
static_assert(std::is_trivially_copy_constructible_v<TypedHandle>);

namespace detail {

inline void assert_plausible_vigil_handle(CrucibleHandle handle) noexcept {
#ifndef NDEBUG
    const auto bits = reinterpret_cast<std::uintptr_t>(handle);
    CRUCIBLE_DEBUG_ASSERT(handle != nullptr);
    CRUCIBLE_DEBUG_ASSERT((bits & (alignof(Vigil) - 1U)) == 0U);
    CRUCIBLE_DEBUG_ASSERT(bits >= 4096U);
#else
    (void)handle;
#endif
}

} // namespace detail

[[nodiscard]] CRUCIBLE_HOT TypedHandle as_vigil_typed(
    CrucibleHandle handle) noexcept
{
    detail::assert_plausible_vigil_handle(handle);
    return TypedHandle{static_cast<Vigil*>(handle)};
}

[[nodiscard]] CRUCIBLE_HOT CrucibleHandle from_typed(
    TypedHandle handle) noexcept
{
    return static_cast<CrucibleHandle>(handle.value());
}

} // namespace crucible::vessel
