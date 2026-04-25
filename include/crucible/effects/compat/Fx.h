#pragma once

// ── crucible::effects::compat — Fx.h backward-compat shim ───────────
//
// Preserves the public spelling of the existing `crucible::fx::*`
// capability tokens (Alloc, IO, Block, Bg, Init, Test) during the
// METX migration.  Production callers never have to change their
// signatures.
//
// STATUS: TEMPORARY SHIM — explicit deletion task #495 (METX-8).
//
// This file currently re-exports the existing crucible/Effects.h
// header verbatim — both old and new effect machinery coexist during
// the transition.  After METX-5 (#477) sweeps the call sites,
// METX-8 (#495) DELETES this file entirely.  METX-9 (#496) closes
// any residual fx::* references in callers that named fx::* as a
// parameter type rather than as a constraint check.  The legacy
// crucible/Effects.h retires last per METX-7 (#479).
//
// Do not depend on this shim long-term.  Do not extend it.
//
// METX-4 (#476) replaces this shim with proper aliases over
// `Computation<Row<{Effect::*}>, void>`:
//
//   namespace crucible::fx {
//       using Bg = ::crucible::effects::Computation<
//           ::crucible::effects::Row<::crucible::effects::Effect::Bg>,
//           void>;
//       // ... and analogous for Alloc / IO / Block / Init / Test
//   }
//
// METX-5 (#477) then sweeps the ~40 production
// `static_assert(has<fx::*, Caps...>)` sites to use
// `requires Subrow<Caps, Row<{Effect::*}>>` per 25_04_2026.md §3.3.
//
// METX-7 (#479) removes crucible/Effects.h entirely and updates
// CLAUDE.md L0 + CRUCIBLE.md to reference the new path.

#include <crucible/Effects.h>
