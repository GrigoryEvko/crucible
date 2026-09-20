#pragma once

// The hardware-instruction atoms.  Every atom here engages
// Axis::HwInstruction.
//
// The ladder is the old HwInstructionLattice's, and its own words say
// what the grades are: a total-order chain over the hardware-instruction
// classes a function may issue, where each tier admits every class below
// it plus its own, so the bottom is the narrowest claim and the top the
// widest.  A tier states intent, not authority.  Declaring the
// privileged tier does not grant ring-0 access; that needs a separate
// ownership token.
//
// ---------------------------------------------------------------------
// The enum lives here, and why
//
// foundation ports no HwInstruction lattice: the old one at
// include/crucible/algebra/lattices/HwInstructionLattice.h was neither
// carried across nor recorded as dropped.  The atoms need the five
// enumerators and their order, so they are declared here verbatim — same
// names, same values — and nothing else of the lattice comes with them.
// A later foundation port can alias to these or these to it; the values
// are the contract either way.
//
// ---------------------------------------------------------------------
// No lift
//
// fixy/atoms/Sync.h gives a lift three meanings: a row names an
// operation and its requirement, the empty row names an operation that
// requires nothing, and no lift names no operation.  An instruction
// class is an admitted SET of operations, not one operation, so the
// family declares no lift.  The one tier that does carry a requirement,
// PrivilegedMsr needing an Init context, is a collision rule (V202)
// reading the Effect row rather than a lift, which is where the old
// catalog read it too.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/effects/Lift.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::atom::hw {

// Old spelling: crucible::algebra::lattices::HwInstruction.
enum class HwInstruction : std::uint8_t {
    NoneAllowed = 0,  // no hardware-specific instruction at all
    Scalar = 1,  // scalar arithmetic and control flow, no SIMD
    Vectorizable = 2,  // SIMD intrinsics
    NonDeterministicTsc = 3,  // rdtsc, rdtscp
    PrivilegedMsr = 4,  // rdmsr, wrmsr, IN, OUT
};

struct none_allowed final : atom_of<Axis::HwInstruction> {
    static constexpr HwInstruction tier = HwInstruction::NoneAllowed;
};

struct scalar final : atom_of<Axis::HwInstruction> {
    static constexpr HwInstruction tier = HwInstruction::Scalar;
};

struct vectorizable final : atom_of<Axis::HwInstruction> {
    static constexpr HwInstruction tier = HwInstruction::Vectorizable;
};

struct non_deterministic_tsc final : atom_of<Axis::HwInstruction> {
    static constexpr HwInstruction tier = HwInstruction::NonDeterministicTsc;
};

struct privileged_msr final : atom_of<Axis::HwInstruction> {
    static constexpr HwInstruction tier = HwInstruction::PrivilegedMsr;
};

// The chain order, which is what "admits every class below it" means for
// a rule: a tier at or above a floor admits everything the floor admits.
[[nodiscard]] consteval bool at_or_above(HwInstruction tier, HwInstruction floor) noexcept {
    return std::to_underlying(tier) >= std::to_underlying(floor);
}

}  // namespace fixy::atom::hw

namespace fixy::atom::detail {

using hw_atom_roster =
    std::tuple<hw::none_allowed, hw::scalar, hw::vectorizable, hw::non_deterministic_tsc, hw::privileged_msr>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::hw_atom_self_test {

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::hw, hw_atom_roster>(),
              "fixy/atoms/Hw.h: an atom declared in fixy::atom::hw is missing from hw_atom_roster.");
static_assert(every_roster_member_is_atom_<hw_atom_roster>(),
              "fixy/atoms/Hw.h: a member of hw_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<hw_atom_roster, Axis::HwInstruction>(),
              "fixy/atoms/Hw.h: every hardware-instruction atom engages Axis::HwInstruction.");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <hw::HwInstruction T>
[[nodiscard]] consteval std::size_t atoms_claiming_() noexcept {
    std::size_t claims = 0;
    template for (constexpr auto member : roster_members_v<hw_atom_roster>) {
        using A = [:member:];
        if constexpr (A::tier == T) ++claims;
    }
    return claims;
}

[[nodiscard]] consteval bool every_tier_has_exactly_one_atom_() noexcept {
    bool exact = true;
    static constexpr auto tiers = std::define_static_array(std::meta::enumerators_of(^^hw::HwInstruction));
    template for (constexpr auto tier_member : tiers) {
        constexpr hw::HwInstruction tier = [:tier_member:];
        exact = exact && (atoms_claiming_<tier>() == 1);
    }
    return exact;
}

[[nodiscard]] consteval bool no_member_lifts_() noexcept {
    bool none = true;
    template for (constexpr auto member : roster_members_v<hw_atom_roster>) {
        using A = [:member:];
        none = none && !::foundation::effects::LiftsToRow<A>;
    }
    return none;
}

#pragma GCC diagnostic pop

static_assert(every_tier_has_exactly_one_atom_(),
              "fixy/atoms/Hw.h: every HwInstruction enumerator must be claimed by exactly one atom in "
              "fixy::atom::hw.  A tier with no atom cannot be written by a caller, and a tier with two means "
              "one of them is unreachable.");

static_assert(no_member_lifts_(), "fixy/atoms/Hw.h: an instruction class admits a set of operations and names none, "
                                  "so no atom here declares lifts_to.  The head of this file says why.");

// The chain, read at both ends and across the one boundary V201 cares
// about: a tier at or above NonDeterministicTsc is one the hot path
// refuses.
static_assert(hw::at_or_above(hw::HwInstruction::PrivilegedMsr, hw::HwInstruction::NonDeterministicTsc));
static_assert(hw::at_or_above(hw::HwInstruction::NonDeterministicTsc, hw::HwInstruction::NonDeterministicTsc));
static_assert(!hw::at_or_above(hw::HwInstruction::Vectorizable, hw::HwInstruction::NonDeterministicTsc));
static_assert(!hw::at_or_above(hw::HwInstruction::NoneAllowed, hw::HwInstruction::Scalar));

static_assert(!std::is_same_v<hw::scalar, hw::vectorizable>);

}  // namespace fixy::atom::detail::hw_atom_self_test
