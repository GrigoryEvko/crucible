#pragma once

// ── crucible::safety umbrella ───────────────────────────────────────
//
// Include this to bring in every safety wrapper at once.  Individual
// headers are available for targeted inclusion in hot-path code to
// minimize compile time.
//
// See code_guide.md §XVI for axiom mapping, usage rules, compiler
// enforcement, and review enforcement rules for each wrapper.

#include <crucible/safety/Checked.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/FileHandle.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Machine.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/OneShotFlag.h>
#include <crucible/safety/Permission.h>
#include <crucible/safety/PermissionFork.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Session.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Tagged.h>
