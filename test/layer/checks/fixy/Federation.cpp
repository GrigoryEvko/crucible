// The compile-time checks of fixy/Federation.h.

#include <fixy/Federation.h>

namespace fixy::federation {

static_assert(sizeof(OrgId) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<OrgId>);

static_assert(std::is_trivially_copyable_v<FederationHandshake>);
static_assert(sizeof(FederationHandshake) == 32);

}  // namespace fixy::federation
