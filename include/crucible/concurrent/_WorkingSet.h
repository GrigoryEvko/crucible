#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

namespace crucible::concurrent {

inline constexpr std::size_t hot_path_cache_line_bytes = 64;
inline constexpr std::size_t unknown_per_call_working_set = std::numeric_limits<std::size_t>::max();

// Lower bounds on the cache of every supported host, for the decisions
// that have to be made before the topology has been read.  They live
// here because this is the one header both consumers already reach:
// the residency-tier classifier and the workload-budget concept.  They
// were two separate sets before, one of them naming the same quantity
// with a _bytes suffix and a different number, so a caller who picked
// the wrong spelling got a different answer to the same question.
//
// Every use is a lower bound, in both directions of comparison.  The
// tier classifier asks "does this footprint fit the cache on every
// supported host?", which is sound only against a value no larger than
// the smallest real cache.  The spread-placement gate asks "is this
// working set certainly cache-resident, so spreading it certainly does
// not pay?", which needs the same bound for the same reason.  Setting
// either above the true minimum makes its claim false on the smallest
// host; setting it below only costs precision.
//
// The figures are the per-core cache of the declared x86 baseline,
// Haswell.  Its L3 varies by part from two to eight megabytes, so four
// sits inside the range rather than above it.  The previous value of
// sixteen exceeded every Haswell client part and the eight-megabyte
// system-level cache of the baseline arm part, so it was not a bound
// at all.
//
// These are not the figures the cost model uses once the topology has
// been read.  Topology probes the real machine, and its own fallbacks
// stand in for a measurement rather than bounding one, so they are
// deliberately larger than these and must stay that way.
inline constexpr std::size_t conservative_l1d_per_core = 32 * 1024;
inline constexpr std::size_t conservative_l2_per_core = 256 * 1024;
inline constexpr std::size_t conservative_l3_total = 4 * 1024 * 1024;

static_assert(conservative_l1d_per_core < conservative_l2_per_core);
static_assert(conservative_l2_per_core < conservative_l3_total);

[[nodiscard]] consteval std::size_t cell_line_footprint(std::size_t value_bytes) noexcept {
    if (value_bytes == 0) return 0;
    return ((value_bytes + hot_path_cache_line_bytes - 1) / hot_path_cache_line_bytes) * hot_path_cache_line_bytes;
}

template <std::size_t ControlLines, typename T>
inline constexpr std::size_t lines_plus_cell_working_set_v =
    ControlLines * hot_path_cache_line_bytes + cell_line_footprint(sizeof(T));

[[nodiscard]] consteval std::size_t saturating_ws_add(std::size_t a, std::size_t b) noexcept {
    if (a == unknown_per_call_working_set || b == unknown_per_call_working_set) {
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
    T, std::void_t<decltype(std::integral_constant<std::size_t, std::remove_cvref_t<T>::per_call_working_set>{})>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_static_per_call_working_set_v = has_static_per_call_working_set<T>::value;

template <typename T>
inline constexpr std::size_t per_call_working_set_of_v = [] consteval {
    if constexpr (has_static_per_call_working_set_v<T>) {
        return std::remove_cvref_t<T>::per_call_working_set;
    } else {
        return unknown_per_call_working_set;
    }
}();

}  // namespace crucible::concurrent
