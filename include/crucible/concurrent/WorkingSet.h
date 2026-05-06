#pragma once

// ── crucible::concurrent working-set facts ─────────────────────────
//
// Shared cache-line and per-call working-set helpers for permissioned
// queues, endpoints, stages, and pipelines.  These are compile-time
// facts only: no probing, no allocation, no runtime checks.

#include <cstddef>
#include <limits>
#include <type_traits>

namespace crucible::concurrent {

inline constexpr std::size_t hot_path_cache_line_bytes = 64;
inline constexpr std::size_t unknown_per_call_working_set =
    std::numeric_limits<std::size_t>::max();

[[nodiscard]] consteval std::size_t
cell_line_footprint(std::size_t value_bytes) noexcept {
    if (value_bytes == 0) return 0;
    return ((value_bytes + hot_path_cache_line_bytes - 1)
                / hot_path_cache_line_bytes) * hot_path_cache_line_bytes;
}

template <std::size_t ControlLines, typename T>
inline constexpr std::size_t lines_plus_cell_working_set_v =
    ControlLines * hot_path_cache_line_bytes
  + cell_line_footprint(sizeof(T));

[[nodiscard]] consteval std::size_t
saturating_ws_add(std::size_t a, std::size_t b) noexcept {
    if (a == unknown_per_call_working_set ||
        b == unknown_per_call_working_set) {
        return unknown_per_call_working_set;
    }
    if (std::numeric_limits<std::size_t>::max() - a < b) {
        return unknown_per_call_working_set;
    }
    return a + b;
}

template <typename T, typename = void>
struct has_static_per_call_working_set : std::false_type {};

template <typename T>
struct has_static_per_call_working_set<
    T,
    std::void_t<decltype(std::integral_constant<
        std::size_t,
        std::remove_cvref_t<T>::per_call_working_set>{})>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_static_per_call_working_set_v =
    has_static_per_call_working_set<T>::value;

template <typename T>
inline constexpr std::size_t per_call_working_set_of_v = [] consteval {
    if constexpr (has_static_per_call_working_set_v<T>) {
        return std::remove_cvref_t<T>::per_call_working_set;
    } else {
        return unknown_per_call_working_set;
    }
}();

}  // namespace crucible::concurrent
