#pragma once

// ── crucible::effects umbrella ──────────────────────────────────────
//
// Include this to pull in the full Met(X) effect-row machinery: the
// Effect atom catalog (Capabilities.h), the Row<...> set algebra
// (EffectRow.h), the Computation<R, T> carrier (Computation.h), and
// the backward-compat shim (compat/Fx.h) preserving the existing
// fx::* spellings.
//
// Hot-path TUs that only need a single piece should include the
// targeted header instead — minimizes compile cost.
//
// Foundation for 25_04_2026.md §3 Met(X) refactor.  Replaces the
// per-token boilerplate in crucible/Effects.h with one row algebra
// that subsumes every fx::* check via Subrow inclusion.  See METX-1
// (#473), METX-2 (#474), METX-3 (#475), METX-4 (#476) for the
// implementation tasks; METX-5 (#477) for the call-site sweep;
// METX-7 (#479) for the legacy-header retirement.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/compat/Fx.h>
