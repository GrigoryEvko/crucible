// The compile-time checks of crucible/warden/Quarantine.h.

#include <crucible/warden/Quarantine.h>

namespace crucible::warden {

static_assert(::foundation::diag::is_diagnostic_class_v<QuarantineTransition>);
static_assert(std::is_trivially_copyable_v<QuarantineSnapshot>);
static_assert(std::is_trivially_copyable_v<QuarantineEvent>);
static_assert(sizeof(QuarantineEvent) <= 64);
static_assert(!std::is_default_constructible_v<QuarantinePolicy<2>>);
static_assert(!std::is_constructible_v<QuarantinePolicy<2>, QuarantineConfig>,
              "The constructor is private.  A policy comes only from mint_quarantine_policy.");
static_assert(CtxFitsQuarantineMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsQuarantineMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsQuarantineRecord<::fixy::BgDrainCtx>);
static_assert(!CtxFitsQuarantineRecord<::fixy::HotFgCtx>);
static_assert(!CtxFitsQuarantineRecord<::fixy::ColdInitCtx>);
static_assert(CtxFitsQuarantineOverride<::fixy::ColdInitCtx>);
static_assert(CtxFitsQuarantineOverride<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsQuarantineOverride<::fixy::BgDrainCtx>);
// Only a context that owns Init mints the operator authority.  A test
// runner can use a token in an override, but it cannot make one.
static_assert(!::foundation::permissions::permission_row_empty(^^quarantine_tag::OperatorOverride));
static_assert(::foundation::permissions::PermissionRootArgs<quarantine_tag::OperatorOverride, ::fixy::ColdInitCtx>);
static_assert(!::foundation::permissions::PermissionRootArgs<quarantine_tag::OperatorOverride, ::fixy::BgDrainCtx>);
static_assert(!::foundation::permissions::PermissionRootArgs<quarantine_tag::OperatorOverride, ::fixy::HotFgCtx>);
static_assert(!::foundation::permissions::PermissionRootArgs<quarantine_tag::OperatorOverride, ::fixy::TestRunnerCtx>);

}  // namespace crucible::warden
