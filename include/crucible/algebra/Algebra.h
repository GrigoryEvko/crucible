#pragma once

// ── crucible::algebra umbrella ──────────────────────────────────────
//
// Include this to pull in the full Graded foundation: the Modality
// enum, the Lattice and Semiring concepts, the `Graded<M, L, T>` core
// template, and every concrete lattice under lattices/.
//
// Hot-path TUs that only need a single lattice or only the Graded core
// should include the targeted header instead — minimizes compile cost.
//
// Foundation for 25_04_2026.md §2 Graded refactor.  Replaces the
// per-wrapper templates in safety/ with type aliases over Graded.
// See misc/25_04_2026.md §2.3 for the alias instantiations and
// CLAUDE.md L0 for the wider safety-stack integration.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>
#include <crucible/algebra/lattices/AllLattices.h>
