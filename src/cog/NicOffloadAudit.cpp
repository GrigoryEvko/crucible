#include <crucible/cog/NicOffloadAudit.h>

namespace crucible::cog {
static_assert(NicOffloadAuditableCog<CogKind::NicPort>);
static_assert(!NicOffloadAuditableCog<CogKind::Gpu>);
static_assert(::foundation::diag::is_diagnostic_class_v<NicOffload_Misconfigured>);
}  // namespace crucible::cog
