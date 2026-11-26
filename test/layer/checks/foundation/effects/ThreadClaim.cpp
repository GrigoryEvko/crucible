// The compile-time checks of foundation/effects/ThreadClaim.h.

#include <foundation/effects/ThreadClaim.h>

#include <foundation/Transfer.h>

#include <type_traits>

namespace foundation::effects {

namespace detail::thread_claim_checks {

// A cell in static storage needs a constant initialization and no
// destructor, and two threads write one cell.  A cell names one holder of one
// object, so it neither copies nor moves.
static_assert(std::is_nothrow_default_constructible_v<ThreadClaimCell>);
static_assert(std::is_trivially_destructible_v<ThreadClaimCell>);
static_assert(::foundation::transfer::Synchronized<ThreadClaimCell>);
static_assert(!std::is_copy_constructible_v<ThreadClaimCell>);
static_assert(!std::is_move_constructible_v<ThreadClaimCell>);
static_assert(!std::is_copy_assignable_v<ThreadClaimCell>);
static_assert(!std::is_move_assignable_v<ThreadClaimCell>);

}  // namespace detail::thread_claim_checks

}  // namespace foundation::effects
