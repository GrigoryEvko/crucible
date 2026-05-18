#pragma once

// ── crucible::effects umbrella ──────────────────────────────────────
//
// Include this to pull in the full Met(X) effect-row machinery: the
// Effect atom catalog (Capabilities.h — also defines the cap::* tag
// types and the Bg / Init / Test context types that mint them), the
// Row<...> set algebra (EffectRow.h), and the Computation<R, T> carrier
// (Computation.h).
//
// Hot-path TUs that only need the cap-tag parameter machinery (the
// large majority — Arena, ExprPool, Graph, MerkleDag, BackgroundThread,
// etc.) include `<crucible/effects/Capabilities.h>` directly to keep
// header parse cost down.  This umbrella is for code that needs the
// full row algebra + Computation carrier (rare today; expected to grow
// as Met(X)-typed signatures replace the cap-tag form per
// 25_04_2026.md §3.3).
//
// fixy-A3-010 (2026-05-18): Resources.h (GAPS-189, 23 ResourceTag axes
// for row-typed resource budgeting) and Concurrent.h (GAPS-190,
// ConcurrentRow additive-sum for stream-overlap scheduling) are part
// of the "full Met(X) effect-row machinery" the umbrella claims to
// provide.  Adding them here closes the surface gap fixy hunters
// caught: every cog/FitsCog consumer + cntp/Backpressure + Fec.h +
// forge/_wip/Phases/Comm.h was reaching for them directly because the
// umbrella didn't expose them.
//
// The legacy `crucible/Effects.h` (fx::Alloc / IO / Block / Bg / Init
// / Test) was deleted in FOUND-B07 / METX-5 (the production sweep);
// no backward-compat shim remains.  See test/test_effects.cpp for the
// canonical surface tests.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/ComputationGraded.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>
#include <crucible/effects/EffectRowProjection.h>
#include <crucible/effects/OsUniverse.h>
#include <crucible/effects/Resources.h>
