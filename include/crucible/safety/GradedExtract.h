#pragma once

// ── crucible::safety::extract — universal GradedWrapper extractors ──
//
// FOUND-D09 of 28_04_2026_effects.md §6.1 + 27_04_2026.md §5.5.
// Universal projections that read the four typedef slots every
// `GradedWrapper`-conforming type exposes, in the canonical
// dispatcher reading namespace `crucible::safety::extract`.
//
// ── What this header ships ──────────────────────────────────────────
//
//   value_type_of_t<W>   The wrapper's USER-facing value type
//                        (W::value_type).  Distinct from substrate's
//                        graded_type::value_type when the wrapper
//                        opts into value_type_decoupled (regime-3 —
//                        AppendOnly's element_type vs container).
//   lattice_of_t<W>      The wrapper's Lattice/Semiring instance
//                        (W::lattice_type).
//   grade_of_t<W>        The lattice's element type
//                        (W::graded_type::grade_type, which is
//                        algebra::LatticeElement<lattice_of_t<W>>).
//   modality_of_v<W>     The wrapper's ModalityKind (W::modality).
//   IsGradedWrapper<W>   Concept form of GradedWrapper for use in
//                        constraint clauses inside the extract
//                        namespace; thin re-export of
//                        algebra::GradedWrapper.
//
// All four are constrained on `IsGradedWrapper<remove_cvref_t<W>>`
// so non-conforming arguments are rejected at the alias declaration
// rather than producing substitution-failure cascades downstream.
//
// ── Why universal extractors are load-bearing ───────────────────────
//
// 27_04 §5.5 names these four extractors as the dispatcher's reading
// surface for grade-aware shape recognizers (FOUND-D12 — D19) and
// row-composition (FOUND-D10).  Without them, every shape concept
// would have to reach into `W::lattice_type` directly, duplicating
// the cv-ref stripping discipline 8× and forcing each concept to
// reimplement the GradedWrapper-conformance check.  With them,
// shape concepts read uniformly:
//
//     concept HotPathOnly = is_hot_path_v<remove_cvref_t<P>>
//                        && lattice_of_t<P>::At<HotPathTier::Hot>
//                        // ... grade match
//
// ── CHEAT-1 awareness ──────────────────────────────────────────────
//
// `value_type_of_t<W>` projects the WRAPPER's `value_type`, NOT the
// substrate's `graded_type::value_type`.  For most wrappers these
// agree.  AppendOnly is the documented exception (regime-3): user-
// facing `value_type = T` (element), substrate
// `graded_type::value_type = Storage<T>` (container).  The
// dispatcher needs to see what the USER declared (T), not what the
// substrate carries (Storage<T>) — so we project W::value_type.
// algebra::GradedTrait.h's CHEAT-1 concept clause already enforces
// the equality unless the wrapper opts out via
// value_type_decoupled, so the projection is correct by
// construction.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// `std::remove_cvref_t<W>` strips reference and top-level cv
// qualifiers before the trait check so call sites can write the
// extractor against the parameter type directly without manual
// decay.  Pointers are NOT stripped — `Linear<int>*` is NOT a
// GradedWrapper, by design.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval projection.
//   TypeSafe — every projection is a typedef alias from a concept-
//              constrained input.  No silent narrowing, no implicit
//              conversion; the algebra::GradedWrapper concept
//              enforces typedef consistency at the substrate level.
//   DetSafe — same W → same projection, deterministically; no
//              hidden state.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/Modality.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── Concept re-export ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The base concept lives in `crucible::algebra`; the dispatcher
// reads through `crucible::safety::extract` for namespace
// uniformity.

template <typename W>
concept IsGradedWrapper =
    ::crucible::algebra::GradedWrapper<std::remove_cvref_t<W>>;

// ═════════════════════════════════════════════════════════════════════
// ── Universal extractors ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Wrapper's user-facing value type.  Per CHEAT-1 (GradedTrait.h),
// this equals `W::graded_type::value_type` UNLESS the wrapper opts
// into `value_type_decoupled` — see header doc.
template <typename W>
    requires IsGradedWrapper<W>
using value_type_of_t = typename std::remove_cvref_t<W>::value_type;

// Wrapper's Lattice / Semiring instance.
template <typename W>
    requires IsGradedWrapper<W>
using lattice_of_t = typename std::remove_cvref_t<W>::lattice_type;

// Lattice's grade element type.  Composes through the substrate:
// W::graded_type::grade_type == algebra::LatticeElement<L>.  We
// project through W::graded_type rather than directly because
// `LatticeElement<L>` requires `Lattice<L>` (concept-constrained),
// which is already discharged by IsGradedWrapper<W>.
template <typename W>
    requires IsGradedWrapper<W>
using grade_of_t =
    typename std::remove_cvref_t<W>::graded_type::grade_type;

// Wrapper's ModalityKind value.  Per CHEAT-5, this matches the
// substrate's modality template arg, so reading W::modality (the
// wrapper's claim) is equivalent to reading
// algebra::graded_modality_v<W::graded_type>.  We project the
// wrapper's claim because the concept constrains them to agree;
// reading the wrapper's surface keeps the diagnostic chain short.
template <typename W>
    requires IsGradedWrapper<W>
inline constexpr ::crucible::algebra::ModalityKind modality_of_v =
    std::remove_cvref_t<W>::modality;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test discipline ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The negative side of the IsGradedWrapper concept can be checked
// in-header without any wrapper instantiation; the positive side
// requires a concrete GradedWrapper-conforming type to instantiate
// the extractors.  Per the FOUND-D03 / FOUND-D04 pattern, the
// sentinel TU `test/test_graded_extract.cpp` ships the full
// per-wrapper instantiation harness using REAL production
// wrappers (Linear, Refined, Tagged, Secret, Stale, Monotonic,
// AppendOnly, HotPath, DetSafe, AllocClass) — covering all four
// ModalityKind values AND all five storage regimes.  Synthetic
// GradedWrapper witnesses fail the concept's CHEAT-3 forwarder-
// fidelity clause (`value_type_name()` strings depend on the
// substrate's reflection-derived name, which is TU-context-
// fragile per `algebra/Graded.h:156-186`); the sentinel TU avoids
// the issue by using already-conformant production wrappers.

namespace detail::graded_extract_self_test {

// Negative-side checks — these don't need any wrapper instance.
static_assert(!IsGradedWrapper<int>);
static_assert(!IsGradedWrapper<int*>);
static_assert(!IsGradedWrapper<void>);
static_assert(!IsGradedWrapper<int&>);
static_assert(!IsGradedWrapper<int(int)>);

// A struct missing the GradedWrapper surface fails the concept.
struct Lookalike_missing_surface { using value_type = int; };
static_assert(!IsGradedWrapper<Lookalike_missing_surface>);

}  // namespace detail::graded_extract_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per the runtime-smoke-test discipline.  The header-side smoke
// tests negative cases only (no wrapper instantiation needed); the
// sentinel TU exercises the full positive matrix at runtime.

inline bool runtime_smoke_test() noexcept {
    using namespace detail::graded_extract_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !IsGradedWrapper<int>;
        ok = ok && !IsGradedWrapper<void>;
        ok = ok && !IsGradedWrapper<Lookalike_missing_surface>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
