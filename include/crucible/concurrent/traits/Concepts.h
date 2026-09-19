#pragma once

#include <crucible/permissions/_Permission.h>

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::concurrent::traits {

template <typename Ch>
concept HasUnifiedTypedefs = requires { typename Ch::value_type; } && requires { typename Ch::user_tag; }
                          && requires { typename Ch::whole_tag; };

template <typename Ch>
concept HasCapacity = requires {
    { Ch::capacity() } noexcept -> std::same_as<std::size_t>;
};

template <typename Ch>
concept HasIsExclusiveActive = requires(const Ch& c) {
    { c.is_exclusive_active() } noexcept -> std::same_as<bool>;
};

template <typename Ch>
concept HasEmptyApprox = requires(const Ch& c) {
    { c.empty_approx() } noexcept -> std::same_as<bool>;
};

template <typename Ch>
concept HasSizeApprox = requires(const Ch& c) {
    { c.size_approx() } noexcept -> std::same_as<std::size_t>;
};

// A channel whose endpoints come from a fractional pool proves quiescence
// through the pool's own atomic state, so the mode transition takes no
// permission argument. The result is true when the body ran and false when the
// pool was still lending.

template <typename Ch>
concept HasPoolDrainedAccess = requires(Ch& c) {
    {
        c.with_drained_access([]() noexcept {})
    } -> std::same_as<bool>;
};

// A channel whose endpoints are all linear has no pool to drain, so the caller
// surrenders the recombined whole permission as the proof instead. The body
// always runs and the permission comes back for a fresh split.

template <typename Ch>
concept HasLinearRecombinedAccess = requires(Ch& c, safety::Permission<typename Ch::whole_tag> p) {
    {
        c.with_recombined_access(std::move(p), []() noexcept {})
    } -> std::same_as<safety::Permission<typename Ch::whole_tag>>;
};

// A channel satisfies exactly one of the two.

template <typename Ch>
concept PoolBasedChannel = HasUnifiedTypedefs<Ch> && HasIsExclusiveActive<Ch> && HasPoolDrainedAccess<Ch>;

template <typename Ch>
concept LinearOnlyChannel = HasUnifiedTypedefs<Ch> && HasIsExclusiveActive<Ch> && HasLinearRecombinedAccess<Ch>;

template <typename Ch>
concept PermissionedChannel = PoolBasedChannel<Ch> || LinearOnlyChannel<Ch>;

template <typename Ch>
concept FifoChannel = PermissionedChannel<Ch> && HasEmptyApprox<Ch> && HasSizeApprox<Ch> && HasCapacity<Ch>;

}  // namespace crucible::concurrent::traits
