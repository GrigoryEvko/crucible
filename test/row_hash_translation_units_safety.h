// The old-tree row hashes that test_row_hash_translation_units_safety
// compares across two translation units.  Each unit includes this header
// and calls row_hashes_seen_here(), which has internal linkage, so each
// unit computes the values in its own context.  An inline function would
// let the linker keep one copy, and the test would compare that copy with
// itself.
//
// The old tree keeps its own row hash fold and its own stable id, which
// fold the printed name of a predicate.  Its predicates are named classes,
// so every entry must agree across the two units.

#pragma once

#include <crucible/safety/_Refined.h>
#include <crucible/safety/_SealedRefined.h>
#include <crucible/safety/diag/_RowHashFold.h>
#include <crucible/safety/diag/_StableName.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace row_hash_translation_units_safety {

inline constexpr std::size_t kEntryCount = 12;

// Defined in the second unit, which declares closures before its
// includes.
std::array<std::uint64_t, kEntryCount> row_hashes_in_shifted_unit() noexcept;
std::string_view printed_late_closure_in_shifted_unit() noexcept;

namespace {

// Complexity: constant, since every value is a compile-time constant.
[[nodiscard]] std::array<std::uint64_t, kEntryCount> row_hashes_seen_here() noexcept {
    namespace cs = ::crucible::safety;
    namespace cd = ::crucible::safety::diag;
    return {
        cd::row_hash_contribution_v<cs::Refined<cs::positive, int>>,
        cd::row_hash_contribution_v<cs::Refined<cs::non_negative, int>>,
        cd::row_hash_contribution_v<cs::Refined<cs::non_zero, int>>,
        cd::row_hash_contribution_v<cs::Refined<cs::is_zero, int>>,
        cd::row_hash_contribution_v<cs::Refined<cs::non_null, int*>>,
        cd::row_hash_contribution_v<cs::Refined<cs::power_of_two, unsigned>>,
        cd::row_hash_contribution_v<cs::Refined<cs::non_empty, std::string_view>>,
        cd::row_hash_contribution_v<cs::Refined<cs::in_range<0, 9>, int>>,
        cd::row_hash_contribution_v<cs::SealedRefined<cs::positive, int>>,
        cd::row_hash_contribution_v<cs::SealedRefined<cs::bounded_above<9>, int>>,
        cd::stable_type_id<std::remove_cvref_t<decltype(cs::positive)>>,
        cd::stable_type_id<cs::Refined<cs::positive, int>>,
    };
}

}  // namespace

}  // namespace row_hash_translation_units_safety
