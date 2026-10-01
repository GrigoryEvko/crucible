// The compile-time checks of fixy/atoms/Simd.h.

#include <fixy/atoms/Simd.h>

namespace fixy::atom::detail::simd_atom_self_test {

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::simd, simd_atom_roster>(),
              "fixy/atoms/Simd.h: an atom declared in fixy::atom::simd is missing from simd_atom_roster.");
static_assert(every_roster_member_is_atom_<simd_atom_roster>(),
              "fixy/atoms/Simd.h: a member of simd_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<simd_atom_roster, Axis::SimdIsa>(),
              "fixy/atoms/Simd.h: every SIMD atom engages Axis::SimdIsa.");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// Every pinned rung is on exactly one of the two trunks, and the two
// shared points are on neither.  This is what keeps the nibble encoding
// honest the day an enumerator is added.
[[nodiscard]] consteval bool trunks_partition_the_pinned_rungs_() noexcept {
    bool partitioned = true;
    static constexpr auto isas = std::define_static_array(std::meta::enumerators_of(^^simd::SimdIsa));
    template for (constexpr auto isa_member : isas) {
        constexpr simd::SimdIsa isa = [:isa_member:];
        constexpr bool x86 = simd::on_x86_trunk(isa);
        constexpr bool arm = simd::on_arm_trunk(isa);
        if constexpr (simd::is_trunk_pinned(isa)) {
            partitioned = partitioned && (x86 != arm);
        } else {
            partitioned = partitioned && !x86 && !arm;
        }
    }
    return partitioned;
}

// Every ISA with a fixed register width has exactly one row in the width
// table, and every other ISA has none.  A new enumerator therefore cannot
// take a width by default.
[[nodiscard]] consteval bool register_bits_table_partitions_the_enum_() noexcept {
    bool exact = true;
    static constexpr auto isas = std::define_static_array(std::meta::enumerators_of(^^simd::SimdIsa));
    template for (constexpr auto isa_member : isas) {
        constexpr simd::SimdIsa isa = [:isa_member:];
        constexpr std::size_t expected_rows = simd::has_fixed_register_width(isa) ? 1U : 0U;
        exact = exact && (simd_register_bits_rows_for_(isa) == expected_rows);
    }
    return exact;
}

// A pinned rung has a real vector register, and the scalar bottom has none.
[[nodiscard]] consteval bool register_bits_match_the_rung_() noexcept {
    bool matched = true;
    for (const auto& row : simd_register_bits_table) {
        const bool vector_width = row.second == 128 || row.second == 256 || row.second == 512;
        matched = matched && (simd::is_trunk_pinned(row.first) ? vector_width : row.second == 0);
    }
    return matched;
}

#pragma GCC diagnostic pop

static_assert(register_bits_table_partitions_the_enum_(),
              "fixy/atoms/Simd.h: every SimdIsa with a fixed register width must have exactly one row in "
              "simd_register_bits_table, and every other SimdIsa none.");
static_assert(register_bits_match_the_rung_(),
              "fixy/atoms/Simd.h: a trunk-pinned ISA has a 128, 256 or 512-bit register, and Scalar has 0.");
static_assert(simd::register_bits(simd::SimdIsa::Scalar) == 0);
static_assert(simd::register_bits(simd::SimdIsa::Sse2) == 128);
static_assert(simd::register_bits(simd::SimdIsa::Sse42) == 128);
static_assert(simd::register_bits(simd::SimdIsa::Avx2) == 256);
static_assert(simd::register_bits(simd::SimdIsa::Avx512Bw) == 512);
static_assert(simd::register_bits(simd::SimdIsa::Neon) == 128);
static_assert(!simd::has_fixed_register_width(simd::SimdIsa::Sve)
              && !simd::has_fixed_register_width(simd::SimdIsa::Sve2)
              && !simd::has_fixed_register_width(simd::SimdIsa::Portable));

static_assert(every_enumerator_has_exactly_one_atom_<simd_atom_roster, simd::SimdIsa>(),
              "fixy/atoms/Simd.h: every SimdIsa enumerator must be claimed by exactly one atom in "
              "fixy::atom::simd.  A rung with no atom cannot be written, and one with two is unreachable.");

static_assert(no_roster_member_lifts_<simd_atom_roster>(),
              "fixy/atoms/Simd.h: an ISA pin names no operation, so no atom here declares lifts_to.  The head "
              "of this file says why.");

static_assert(trunks_partition_the_pinned_rungs_(),
              "fixy/atoms/Simd.h: a pinned rung must be on exactly one vendor trunk and the two shared points on "
              "neither.  An enumerator was added with a nibble the trunk predicates do not classify.");

static_assert(!simd::is_trunk_pinned(simd::SimdIsa::Scalar) && !simd::is_trunk_pinned(simd::SimdIsa::Portable));
static_assert(simd::on_x86_trunk(simd::SimdIsa::Avx2) && simd::on_arm_trunk(simd::SimdIsa::Sve2));
static_assert(!std::is_same_v<simd::avx2, simd::avx512f>);

}  // namespace fixy::atom::detail::simd_atom_self_test
