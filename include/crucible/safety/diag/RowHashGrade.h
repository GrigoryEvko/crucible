#pragma once

// ── safety/diag/RowHashGrade.h ──────────────────────────────────────
//
// FIXY-FOUND-059: runtime-state extension to the type-level row_hash.
//
// The four Regime-4 product-lattice wrappers — Budgeted, EpochVersioned,
// NumaPlacement, RecipeSpec — each carry a meaningful per-instance
// runtime grade alongside the payload T (Budgeted: 16 B bits+peak;
// EpochVersioned: 16 B epoch+generation; NumaPlacement: 33 B node+
// affinity; RecipeSpec: 2 B tolerance+family).  The TYPE-level
// row_hash_contribution<W<T>> in RowHashFold.h folds ONLY the wrapper
// tag salt over Inner's contribution; the per-instance grade is
// intentionally invisible at that layer because row_hash is a
// federation cache SLOT KEY (type identity, not instance identity).
//
// Some downstream consumers need INSTANCE discrimination — drift
// attribution (two NumaPlacement<TensorMeta> at distinct nodes are
// distinct instances even though they hash to the same cache slot),
// instance-cache (per-instance memoization), observability rollup
// (sum-of-hashes over fleet membership for change-detection).  For
// those callers we ship a separate runtime function
// `row_hash_with_grade(w)` that returns
//
//   combine_ids(row_hash_contribution_v<W>, grade_hash(w))
//
// where the grade extraction is opt-in via a customization point.
//
// ── Customization point: row_hash_grade_extractor<W> ─────────────────
//
// The primary template is INTENTIONALLY UNDEFINED.  A wrapper that
// has a meaningful runtime grade ships an explicit specialization
// defining `static constexpr uint64_t extract(const W&) noexcept`;
// wrappers without runtime grade do NOT specialize — calling
// `row_hash_with_grade(w)` on such a type is a hard compile error
// (caught by the function's `requires` clause), making misuse
// audit-discoverable.
//
// This file ships specializations for the four canonical Regime-4
// wrappers.  Future wrappers with runtime grade (a hypothetical
// `StalePlacement<T>`, a `TimeBudgeted<T>`, ...) add their own
// specializations alongside their declaration.
//
// ── Design vs row_hash_contribution<W> (RowHashFold.h) ───────────────
//
//   row_hash_contribution_v<W>     →  COMPILE-TIME, type-level slot key.
//                                     Folds wrapper tag + Inner's hash.
//                                     Used by federation cache routing.
//
//   row_hash_with_grade(w)         →  RUNTIME, instance discriminator.
//                                     Folds row_hash_contribution_v<W>
//                                     + per-instance grade extraction.
//                                     Used by drift attribution / per-
//                                     instance memoization / fleet
//                                     change detection.
//
// Both functions are deterministic (same inputs → same outputs);
// `row_hash_with_grade` reads runtime fields so it cannot be
// consteval, but it remains constexpr-callable when the W instance
// itself is a constant expression (e.g. constexpr Budgeted in a
// test fixture).
//
//   Axiom coverage:
//     InitSafe — all extractions read fully-initialized grade fields.
//     TypeSafe — strong-typed accessors; enum cast via
//                std::to_underlying / static_cast<underlying_type>.
//                No reinterpret_cast.
//     NullSafe — by-const-ref parameter; no pointer deref.
//     MemSafe  — header-only, no allocation.
//     BorrowSafe — by-const-ref read only.
//     ThreadSafe — pure read on caller's W; thread-safety inherited.
//     LeakSafe — no resources.
//     DetSafe — combine_ids is deterministic; grade reads are pure;
//               same w → same hash on every build, every platform.
//
//   Citation: 25_04_2026.md §2.4 (Budgeted), §2.5 (EpochVersioned),
//             §2.6 (NumaPlacement), §2.7 (RecipeSpec); FOUND-G63/G68
//             (Regime-4 product-lattice substrate); FOUND-FOUND-050
//             (combine_ids single source of truth).

#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/safety/diag/StableName.h>  // combine_ids

#include <crucible/safety/Budgeted.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/RecipeSpec.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>  // std::to_underlying

namespace crucible::safety::diag {

// ── Primary template — intentionally undefined ───────────────────────
//
// A specialization MUST define
//   static constexpr std::uint64_t extract(const W&) noexcept;
//
// Wrappers without runtime grade do NOT specialize.  Calling
// `row_hash_with_grade` on such a type triggers a clean compile
// error via the function's `requires` clause (concept satisfaction
// fails because the static member is missing).
template <typename W>
struct row_hash_grade_extractor;

// ── Concept: does W ship a row_hash_grade_extractor specialization? ──
//
// Used as the requires-clause gate on `row_hash_with_grade(w)`.  A
// wrapper with no specialization is rejected at the call site.
template <typename W>
concept HasRowHashGradeExtractor = requires(const W& w) {
    { row_hash_grade_extractor<W>::extract(w) }
        -> std::convertible_to<std::uint64_t>;
};

// ── Free function: runtime hash including grade ──────────────────────
//
// Folds the type-level row_hash_contribution with the extracted
// runtime grade hash.  Constexpr-callable when W instance is a
// constant expression; otherwise a regular noexcept runtime read.
template <typename W>
    requires HasRowHashGradeExtractor<W>
[[nodiscard]] constexpr std::uint64_t row_hash_with_grade(const W& w) noexcept {
    return detail::combine_ids(
        row_hash_contribution_v<W>,
        row_hash_grade_extractor<W>::extract(w));
}

// ── Specializations for the four Regime-4 wrappers ──────────────────

// Budgeted<T>: grade = (BitsBudget bits, PeakBytes peak), 16 bytes.
// Both axes are strong-typed uint64_t newtypes; we extract .value
// from each and fold via combine_ids in canonical (bits, peak) order.
template <typename Inner>
struct row_hash_grade_extractor<safety::Budgeted<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(
        const safety::Budgeted<Inner>& w) noexcept
    {
        return detail::combine_ids(
            static_cast<std::uint64_t>(w.bits().value),
            static_cast<std::uint64_t>(w.peak_bytes().value));
    }
};

// EpochVersioned<T>: grade = (Epoch epoch, Generation gen), 16 bytes.
// Canonical fold order: epoch first, then generation.
template <typename Inner>
struct row_hash_grade_extractor<safety::EpochVersioned<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(
        const safety::EpochVersioned<Inner>& w) noexcept
    {
        return detail::combine_ids(
            static_cast<std::uint64_t>(w.epoch().value),
            static_cast<std::uint64_t>(w.generation().value));
    }
};

// NumaPlacement<T>: grade = (NumaNodeId node : uint8_t enum,
// AffinityMask affinity : 4×uint64_t).  Canonical fold:
//   start_seed = node enum value (zero-extended to u64)
//   then combine with each of the 4 affinity words in order.
template <typename Inner>
struct row_hash_grade_extractor<safety::NumaPlacement<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(
        const safety::NumaPlacement<Inner>& w) noexcept
    {
        std::uint64_t h = static_cast<std::uint64_t>(
            std::to_underlying(w.numa_node()));
        auto const& aff = w.affinity();
        for (std::size_t i = 0; i < safety::AffinityMask::kWords; ++i) {
            h = detail::combine_ids(h, aff.words[i]);
        }
        return h;
    }
};

// RecipeSpec<T>: grade = (Tolerance tier : uint8_t enum,
// RecipeFamily family : uint8_t enum), 2 bytes.  Canonical fold:
// tolerance first, then family.
template <typename Inner>
struct row_hash_grade_extractor<safety::RecipeSpec<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(
        const safety::RecipeSpec<Inner>& w) noexcept
    {
        return detail::combine_ids(
            static_cast<std::uint64_t>(
                std::to_underlying(w.tolerance())),
            static_cast<std::uint64_t>(
                std::to_underlying(w.recipe_family())));
    }
};

}  // namespace crucible::safety::diag
