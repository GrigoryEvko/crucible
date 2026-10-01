// The compile-time checks of crucible/cntp/Backpressure.h.

#include <crucible/cntp/Backpressure.h>

namespace crucible::cntp {

static_assert(sizeof(PositiveBackpressureBytes) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveConnectionLimit) == sizeof(std::uint16_t));
static_assert(sizeof(ResourcePressurePpm) == sizeof(std::uint32_t));
static_assert(sizeof(ResourceLimitPpm) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredAdmissionDecision) == sizeof(AdmissionDecision));
static_assert(std::is_trivially_copy_constructible_v<ConnectionRequest>
              && std::is_trivially_destructible_v<ConnectionRequest>);
static_assert(std::is_trivially_copy_constructible_v<ResourcePressure>
              && std::is_trivially_destructible_v<ResourcePressure>);
static_assert(std::is_trivially_copy_constructible_v<ResourceLimit> && std::is_trivially_destructible_v<ResourceLimit>);
static_assert(std::is_trivially_copy_constructible_v<AdmissionDecision>
              && std::is_trivially_destructible_v<AdmissionDecision>);

}  // namespace crucible::cntp
