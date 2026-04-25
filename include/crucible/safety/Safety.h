#pragma once

// ── crucible::safety umbrella — value-level wrappers only ───────────
//
// This umbrella aggregates the OBJECT-LEVEL invariant wrappers — the
// primitives that decorate a value with a compile-time predicate /
// classification / linearity / typestate / mutation discipline.
//
// Other substrates have their own umbrellas:
//   <crucible/permissions/Permissions.h>  — CSL ownership family
//   <crucible/handles/Handles.h>          — RAII resource handles
//   <crucible/sessions/Sessions.h>        — protocol-level session types
//   <crucible/bridges/Bridges.h>          — cross-substrate composition
//   <crucible/algebra/Algebra.h>          — Graded<>+lattice foundation
//   <crucible/effects/Effects.h>          — Met(X) effect rows
//
// See code_guide.md §XVI for axiom mapping, usage rules, compiler
// enforcement, and review enforcement rules for each wrapper.
// 25_04_2026.md §2 documents the directory split rationale.

#include <crucible/safety/Checked.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Machine.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Workload.h>
