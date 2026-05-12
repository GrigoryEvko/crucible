#include <crucible/cog/NumaNic.h>

namespace crucible::cog {
static_assert(NumaNicAuditableCog<CogKind::NicPort>);
static_assert(!NumaNicAuditableCog<CogKind::Gpu>);
static_assert(safety::diag::is_diagnostic_class_v<NumaNic_Misaligned>);
static_assert(sizeof(NumaNodeId) == sizeof(std::uint16_t));
}  // namespace crucible::cog
