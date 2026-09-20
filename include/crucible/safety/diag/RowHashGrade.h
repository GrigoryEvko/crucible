#pragma once

// The type-level row hash identifies a type and routes a cache slot. It
// deliberately ignores the runtime grade a wrapper carries, so two
// instances that differ only in that grade share one slot.
//
// The function here is the other question: which instance is this. It
// folds the runtime grade over the type-level hash, for callers doing
// drift attribution, per-instance memoization or change detection across
// a fleet. It is not a cache slot key and must not be used as one.

#include <crucible/safety/diag/_RowHashFold.h>
#include <crucible/safety/diag/_StableName.h>

#include <crucible/safety/Budgeted.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/_RecipeSpec.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety::diag {

// The primary template stays undefined. A wrapper carrying a runtime
// grade opts in by specializing it, and one that does not is rejected at
// the call site rather than silently hashed without its grade.
template <typename W>
struct row_hash_grade_extractor;

template <typename W>
concept HasRowHashGradeExtractor = requires(const W& w) {
    { row_hash_grade_extractor<W>::extract(w) } -> std::convertible_to<std::uint64_t>;
};

template <typename W>
    requires HasRowHashGradeExtractor<W>
[[nodiscard]] constexpr std::uint64_t row_hash_with_grade(const W& w) noexcept {
    return detail::combine_ids(row_hash_contribution_v<W>, row_hash_grade_extractor<W>::extract(w));
}

// The fold is order-sensitive, so the order each extractor visits its
// fields in is part of the value it produces. Changing an order changes
// every hash that extractor has produced.

template <typename Inner>
struct row_hash_grade_extractor<safety::Budgeted<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(const safety::Budgeted<Inner>& w) noexcept {
        return detail::combine_ids(static_cast<std::uint64_t>(w.bits().value),
                                   static_cast<std::uint64_t>(w.peak_bytes().value));
    }
};

template <typename Inner>
struct row_hash_grade_extractor<safety::EpochVersioned<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(const safety::EpochVersioned<Inner>& w) noexcept {
        return detail::combine_ids(static_cast<std::uint64_t>(w.epoch().value),
                                   static_cast<std::uint64_t>(w.generation().value));
    }
};

// The node seeds the fold and the affinity words follow in index order.
template <typename Inner>
struct row_hash_grade_extractor<safety::NumaPlacement<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(const safety::NumaPlacement<Inner>& w) noexcept {
        std::uint64_t h = static_cast<std::uint64_t>(std::to_underlying(w.numa_node()));
        auto const& aff = w.affinity();
        for (std::size_t i = 0; i < safety::AffinityMask::kWords; ++i) {
            h = detail::combine_ids(h, aff.words[i]);
        }
        return h;
    }
};

template <typename Inner>
struct row_hash_grade_extractor<safety::RecipeSpec<Inner>> {
    [[nodiscard]] static constexpr std::uint64_t extract(const safety::RecipeSpec<Inner>& w) noexcept {
        return detail::combine_ids(static_cast<std::uint64_t>(std::to_underlying(w.tolerance())),
                                   static_cast<std::uint64_t>(std::to_underlying(w.recipe_family())));
    }
};

}  // namespace crucible::safety::diag
