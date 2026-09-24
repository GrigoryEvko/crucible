// The row hashes that test_row_hash_translation_units compares across
// two translation units.  Each unit includes this header and calls
// row_hashes_seen_here(), which has internal linkage, so each unit
// computes the values in its own context.  An inline function would let
// the linker keep one copy, and the test would compare that copy with
// itself.
//
// Every entry folds a reflected name.  The refinements fold the name of
// their predicate's class, which is the case that depended on the unit
// when the predicates were closures.

#pragma once

#include <fixy/Fn.h>
#include <fixy/Refined.h>
#include <fixy/Secret.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace row_hash_translation_units {

inline constexpr std::size_t kEntryCount = 16;

// Defined in the second unit, which declares closures before its
// includes.
std::array<std::uint64_t, kEntryCount> row_hashes_in_shifted_unit() noexcept;
std::string_view printed_late_closure_in_shifted_unit() noexcept;

namespace {

// Complexity: constant, since every value is a compile-time constant.
[[nodiscard]] std::array<std::uint64_t, kEntryCount> row_hashes_seen_here() noexcept {
    namespace fd = ::foundation::diag;
    namespace fr = ::foundation::reflect;
    return {
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::positive, int>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::non_negative, int>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::non_zero, int>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::is_zero, int>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::non_null, int*>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::power_of_two, unsigned>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::non_empty, std::string_view>>,
        fd::row_hash_contribution_v<::fixy::SealedRefined<::fixy::positive, int>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::in_range<0, 9>, int>>,
        fd::row_hash_contribution_v<::fixy::Refined<::fixy::all_of<::fixy::positive, ::fixy::bounded_above<9>>, int>>,
        fd::row_hash_contribution_v<::fixy::Tagged<int, ::fixy::tags::trust::Verified>>,
        fd::row_hash_contribution_v<::fixy::Secret<int>>,
        fd::row_hash_contribution_v<::fixy::fn<int>>,
        fr::stable_type_id<::fixy::IsPositive>,
        fr::stable_type_id<::fixy::Refined<::fixy::positive, int>>,
        fd::federation_key_with_toolchain_v<::fixy::Refined<::fixy::positive, int>>,
    };
}

}  // namespace

}  // namespace row_hash_translation_units
