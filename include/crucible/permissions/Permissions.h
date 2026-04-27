#pragma once

// ── crucible::permissions umbrella ──────────────────────────────────
//
// CSL ownership family: Permission<Tag> (linear singleton token),
// SharedPermission<Tag> (fractional refcounted share — currently
// inside Permission.h; MIGRATE-7 / #467 splits to SharedPermission.h),
// PermissionFork (CSL parallel-rule fork-join), ReadView<Tag>
// (lifetime-bound shared borrow).
//
// Hot-path TUs that only need a single primitive should include the
// targeted header directly to minimize compile cost.

#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/PermSet.h>
#include <crucible/permissions/ReadView.h>
