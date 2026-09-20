#pragma once

// The SIMD-ISA atoms.  Every atom here engages Axis::SimdIsa.
//
// The grades are the old SimdIsaLattice's, and its shape is the same as
// MemoryScope's: two vendor trunks that meet only at a shared bottom and
// a shared top.  Scalar is the bottom — no SIMD at all, so it runs on
// any processor — and Portable is the top, one kernel that runs under
// any instruction set.  Between them the x86 trunk (SSE2 through
// AVX-512BW) and the ARM trunk (NEON through SVE2) order internally and
// have no relation to one another: x86 code never runs on ARM and vice
// versa, and the old lattice's cross-trunk leq was false for exactly
// that reason.  The trunk is the high nibble of the enumerator value.
//
// fixy/Axis.h files this under Tier L rather than Tier S for the same
// reason as MemoryScope: a non-distributive partial order.
//
// ---------------------------------------------------------------------
// The enum lives here, and why
//
// foundation ports no SimdIsa lattice, and the old one at
// include/crucible/algebra/lattices/SimdIsaLattice.h was neither carried
// across nor recorded in port-drops.txt.  The fifteen enumerators and
// their values are restated here verbatim, with the trunk nibble they
// encode, and nothing else of the lattice comes with them.  A later
// foundation port can alias either way; the values are the contract.
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

#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::atom::simd {

// Old spelling: crucible::algebra::lattices::SimdIsa.  The high nibble
// names the trunk: 0x0 the shared bottom, 0x1 x86, 0x2 ARM, 0xF the
// shared top.
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
// V101 and V402 stand down for both.  This is the old
// simd_isa_pins_specific_vector, read from the value.
[[nodiscard]] consteval bool is_trunk_pinned(SimdIsa isa) noexcept {
    return isa != SimdIsa::Scalar && isa != SimdIsa::Portable;
}

[[nodiscard]] consteval bool on_x86_trunk(SimdIsa isa) noexcept { return trunk_of(isa) == 0x1; }
[[nodiscard]] consteval bool on_arm_trunk(SimdIsa isa) noexcept { return trunk_of(isa) == 0x2; }

}  // namespace fixy::atom::simd

namespace fixy::atom::detail {

using simd_atom_roster = std::tuple<simd::scalar, simd::sse2, simd::sse3, simd::ssse3, simd::sse41, simd::sse42,
                                    simd::avx2, simd::avx512f, simd::avx512bw, simd::neon, simd::neon_fp16,
                                    simd::neon_dot_product, simd::sve, simd::sve2, simd::portable>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::simd_atom_self_test {

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::simd, simd_atom_roster>(),
              "fixy/atoms/Simd.h: an atom declared in fixy::atom::simd is missing from simd_atom_roster.");
static_assert(every_roster_member_is_atom_<simd_atom_roster>(),
              "fixy/atoms/Simd.h: a member of simd_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<simd_atom_roster, Axis::SimdIsa>(),
              "fixy/atoms/Simd.h: every SIMD atom engages Axis::SimdIsa.");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <simd::SimdIsa I>
[[nodiscard]] consteval std::size_t atoms_claiming_() noexcept {
    std::size_t claims = 0;
    template for (constexpr auto member : roster_members_v<simd_atom_roster>) {
        using A = [:member:];
        if constexpr (A::isa == I) ++claims;
    }
    return claims;
}

[[nodiscard]] consteval bool every_isa_has_exactly_one_atom_() noexcept {
    bool exact = true;
    static constexpr auto isas = std::define_static_array(std::meta::enumerators_of(^^simd::SimdIsa));
    template for (constexpr auto isa_member : isas) {
        constexpr simd::SimdIsa isa = [:isa_member:];
        exact = exact && (atoms_claiming_<isa>() == 1);
    }
    return exact;
}

[[nodiscard]] consteval bool no_member_lifts_() noexcept {
    bool none_lift = true;
    template for (constexpr auto member : roster_members_v<simd_atom_roster>) {
        using A = [:member:];
        none_lift = none_lift && !::foundation::effects::LiftsToRow<A>;
    }
    return none_lift;
}

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

#pragma GCC diagnostic pop

static_assert(every_isa_has_exactly_one_atom_(),
              "fixy/atoms/Simd.h: every SimdIsa enumerator must be claimed by exactly one atom in "
              "fixy::atom::simd.  A rung with no atom cannot be written, and one with two is unreachable.");

static_assert(no_member_lifts_(), "fixy/atoms/Simd.h: an ISA pin names no operation, so no atom here declares "
                                  "lifts_to.  The head of this file says why.");

static_assert(trunks_partition_the_pinned_rungs_(),
              "fixy/atoms/Simd.h: a pinned rung must be on exactly one vendor trunk and the two shared points on "
              "neither.  An enumerator was added with a nibble the trunk predicates do not classify.");

static_assert(!simd::is_trunk_pinned(simd::SimdIsa::Scalar) && !simd::is_trunk_pinned(simd::SimdIsa::Portable));
static_assert(simd::on_x86_trunk(simd::SimdIsa::Avx2) && simd::on_arm_trunk(simd::SimdIsa::Sve2));
static_assert(!std::is_same_v<simd::avx2, simd::avx512f>);

}  // namespace fixy::atom::detail::simd_atom_self_test
