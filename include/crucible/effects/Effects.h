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
// The legacy `crucible/Effects.h` (fx::Alloc / IO / Block / Bg / Init
// / Test) was deleted in FOUND-B07 / METX-5 (the production sweep);
// no backward-compat shim remains.  See test/test_effects.cpp for the
// canonical surface tests.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>
#include <crucible/effects/OsUniverse.h>
