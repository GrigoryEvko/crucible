#pragma once

// The SIMD-ISA atoms.  Every atom here engages Axis::SimdIsa.
//
// The grades have the same shape as MemoryScope's: two vendor trunks
// that meet only at a shared bottom and a shared top.  Scalar is the
// bottom — no SIMD at all, so it runs on any processor — and Portable is
// the top, one kernel that runs under any instruction set.  Between them
// the x86 trunk (SSE2 through AVX-512BW) and the ARM trunk (NEON through
// SVE2) order internally and have no relation to one another, because
// x86 code never runs on ARM and ARM code never runs on x86.  The trunk
// is the high nibble of the enumerator value.
//
// fixy/Axis.h files this under Tier L rather than Tier S for the same
// reason as MemoryScope: a non-distributive partial order.
//
// ---------------------------------------------------------------------
// The enum lives here, and why
//
// foundation has no SimdIsa lattice.  The fifteen enumerators and their
// values are declared here, with the trunk nibble that they encode, and
// nothing else of a lattice.  A lattice that foundation adds later can
// alias either way.  The values are the contract.
//
// ---------------------------------------------------------------------
// No lift
//
// The third of fixy/atoms/Sync.h's three readings.  A pinned ISA is the
// instruction set a body was EMITTED for, not an operation it performs
// on a surface a context must admit.  What an ISA pin does to replay is
// V101's business and whether it is coherent with a memory scope is
// V402's; both are collision rules.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/effects/Lift.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::atom::simd {

inline constexpr atom_seal atom_namespace_seal{};

// The high nibble names the trunk: 0x0 the shared bottom, 0x1 x86, 0x2
// ARM, 0xF the shared top.
enum class SimdIsa : std::uint8_t {
    Scalar = 0x00,  // no SIMD at all, so it runs on any processor
    Sse2 = 0x10,
    Sse3 = 0x11,
    Ssse3 = 0x12,
    Sse41 = 0x13,
    Sse42 = 0x14,
    Avx2 = 0x15,  // AVX2 together with BMI2 and FMA
    Avx512F = 0x16,
    Avx512Bw = 0x17,
    Neon = 0x20,
    NeonFp16 = 0x21,
    NeonDotProduct = 0x22,
    Sve = 0x23,
    Sve2 = 0x24,
    Portable = 0xFF,  // one kernel that runs under any instruction set
};

// The shared bottom.
struct scalar final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Scalar;
};

// The x86 trunk.
struct sse2 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Sse2;
};
struct sse3 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Sse3;
};
struct ssse3 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Ssse3;
};
struct sse41 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Sse41;
};
struct sse42 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Sse42;
};
struct avx2 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Avx2;
};
struct avx512f final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Avx512F;
};
struct avx512bw final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Avx512Bw;
};

// The ARM trunk.
struct neon final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Neon;
};
struct neon_fp16 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::NeonFp16;
};
struct neon_dot_product final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::NeonDotProduct;
};
struct sve final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Sve;
};
struct sve2 final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Sve2;
};

// The shared top.
struct portable final : atom_of<Axis::SimdIsa> {
    static constexpr SimdIsa isa = SimdIsa::Portable;
};

// The trunk nibble, which is the whole of what the rules read.
[[nodiscard]] consteval std::uint8_t trunk_of(SimdIsa isa) noexcept {
    return static_cast<std::uint8_t>(std::to_underlying(isa) >> 4);
}

// Whether an ISA is pinned to ONE vendor trunk — neither the shared
// bottom nor the shared top.  Scalar runs anywhere and Portable is by
// definition one kernel for every set, so neither pins anything and
// V101 and V402 stand down for both.
[[nodiscard]] consteval bool is_trunk_pinned(SimdIsa isa) noexcept {
    return isa != SimdIsa::Scalar && isa != SimdIsa::Portable;
}

[[nodiscard]] consteval bool on_x86_trunk(SimdIsa isa) noexcept { return trunk_of(isa) == 0x1; }
[[nodiscard]] consteval bool on_arm_trunk(SimdIsa isa) noexcept { return trunk_of(isa) == 0x2; }

// Whether an ISA has one fixed vector-register width.  SVE and SVE2 are
// scalable, so the width belongs to the processor and not to the ISA.
// Portable names no register at all.
[[nodiscard]] consteval bool has_fixed_register_width(SimdIsa isa) noexcept {
    return isa != SimdIsa::Sve && isa != SimdIsa::Sve2 && isa != SimdIsa::Portable;
}

}  // namespace fixy::atom::simd

namespace fixy::atom::detail {

using simd_atom_roster = std::tuple<simd::scalar, simd::sse2, simd::sse3, simd::ssse3, simd::sse41, simd::sse42,
                                    simd::avx2, simd::avx512f, simd::avx512bw, simd::neon, simd::neon_fp16,
                                    simd::neon_dot_product, simd::sve, simd::sve2, simd::portable>;

// One row for each ISA with a fixed register width, and no row for the
// others.  The self-test below holds that partition against the enum.
inline constexpr std::array<std::pair<simd::SimdIsa, std::uint16_t>, 12> simd_register_bits_table{{
    {simd::SimdIsa::Scalar, 0},
    {simd::SimdIsa::Sse2, 128},
    {simd::SimdIsa::Sse3, 128},
    {simd::SimdIsa::Ssse3, 128},
    {simd::SimdIsa::Sse41, 128},
    {simd::SimdIsa::Sse42, 128},
    {simd::SimdIsa::Avx2, 256},
    {simd::SimdIsa::Avx512F, 512},
    {simd::SimdIsa::Avx512Bw, 512},
    {simd::SimdIsa::Neon, 128},
    {simd::SimdIsa::NeonFp16, 128},
    {simd::SimdIsa::NeonDotProduct, 128},
}};

[[nodiscard]] consteval std::size_t simd_register_bits_rows_for_(simd::SimdIsa isa) noexcept {
    std::size_t rows = 0;
    for (const auto& row : simd_register_bits_table)
        rows += (row.first == isa) ? 1U : 0U;
    return rows;
}

[[nodiscard]] consteval std::uint16_t simd_register_bits_of_(simd::SimdIsa isa) noexcept {
    std::uint16_t bits = 0;
    for (const auto& row : simd_register_bits_table)
        if (row.first == isa) bits = row.second;
    return bits;
}

}  // namespace fixy::atom::detail

namespace fixy::atom::simd {

// The width of one vector register, in bits.  Scalar has no vector
// register, so its width is 0.  A kernel that steps one register per
// iteration checks its stride against this value.  An ISA without a fixed
// width has no value here, and naming one does not compile.
template <SimdIsa Isa>
    requires(has_fixed_register_width(Isa))
inline constexpr std::uint16_t register_bits_v = ::fixy::atom::detail::simd_register_bits_of_(Isa);

}  // namespace fixy::atom::simd

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

template <simd::SimdIsa I>
concept names_register_bits_ = requires { simd::register_bits_v<I>; };

#pragma GCC diagnostic pop

static_assert(register_bits_table_partitions_the_enum_(),
              "fixy/atoms/Simd.h: every SimdIsa with a fixed register width must have exactly one row in "
              "simd_register_bits_table, and every other SimdIsa none.");
static_assert(register_bits_match_the_rung_(),
              "fixy/atoms/Simd.h: a trunk-pinned ISA has a 128, 256 or 512-bit register, and Scalar has 0.");
static_assert(simd::register_bits_v<simd::SimdIsa::Scalar> == 0);
static_assert(simd::register_bits_v<simd::SimdIsa::Sse2> == 128);
static_assert(simd::register_bits_v<simd::SimdIsa::Avx2> == 256);
static_assert(simd::register_bits_v<simd::SimdIsa::Avx512Bw> == 512);
static_assert(simd::register_bits_v<simd::SimdIsa::Neon> == 128);
static_assert(names_register_bits_<simd::SimdIsa::Sse42>);
static_assert(!names_register_bits_<simd::SimdIsa::Sve>);
static_assert(!names_register_bits_<simd::SimdIsa::Sve2>);
static_assert(!names_register_bits_<simd::SimdIsa::Portable>);

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
