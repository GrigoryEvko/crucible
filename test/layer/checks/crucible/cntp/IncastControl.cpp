// The compile-time checks of crucible/cntp/IncastControl.h.

#include <crucible/cntp/IncastControl.h>

namespace crucible::cntp {

static_assert(sizeof(PositiveCreditBytes) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveRtoMinUsec) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveSenderCount) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredIncastConfig) == sizeof(IncastConfig));
// A refined field keeps the config from being trivially copyable, so no
// byte copy builds one past the predicates.  The copy and the destructor
// stay trivial, so the config still passes as a plain value.
static_assert(std::is_trivially_copy_constructible_v<IncastConfig> && std::is_trivially_destructible_v<IncastConfig>);
static_assert(CtxFitsIncastApply<detail::socket_option_invariants::IoBlockCtx>);
static_assert(!CtxFitsIncastApply<detail::socket_option_invariants::IoOnlyCtx>,
              "a context without Block can neither read the algorithm list nor set a socket option.");

}  // namespace crucible::cntp
