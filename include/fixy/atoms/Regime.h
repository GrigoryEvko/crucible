#pragma once

// The regime atoms.  Every atom here engages Axis::Regime.
//
// Regime is where in the latency budget a function runs.  It is not
// Complexity: Complexity bounds the asymptotic and the termination
// class, Regime bounds the wall clock.  Neither subsumes the other, and
// fixy/Axis.h says so at the enumerator.
//
// ---------------------------------------------------------------------
// Why this axis carries atoms at all, when its discharge is Measurement
//
// axis_traits<Axis::Regime>::discharge is Discharge::Measurement: which
// tier a function actually achieves is settled by the bench, not by the
// type.  An atom here therefore declares an INTENT, and the bench is
// what confirms it.  That is still worth a grade, because a large class
// of hot-path mistakes is decidable from the intent alone without
// measuring anything: a body that declares itself hot and also declares
// unbounded cost, or a Bg row, or buffered stdio, or a coroutine frame,
// is refused by fixy/Collision.h's H and R and S families before a
// bench ever runs.  The measurement settles the cases that survive the
// contradictions, not the contradictions themselves.
//
// This is the same division the project draws everywhere else: the type
// system refuses what is impossible, and measurement chooses among what
// is possible.
//
// ---------------------------------------------------------------------
// No lift
//
// The atoms here declare no `lifts_to`, and that is not an omission.  A
// latency budget is not an effect: running on the hot path neither
// performs I/O nor allocates nor blocks, it only forbids doing so.  The
// forbidding is what the collision rules express.  An atom lifts when
// it NAMES an operation the context has to admit, which is why the
// SyscallSurface families of fixy/atoms/Os.h lift and this one does not.
//
// The strict pole on this axis is Unconstrained, so a binding that
// writes no regime atom claims no budget and every H, R and S rule
// stands down.  That is deliberate: most functions in the tree are
// neither hot nor cold in any sense worth typing, and reject-by-default
// on this axis would demand a tier from all of them.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/algebra/lattices/HotPathLattice.h>

#include <cstddef>
#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::atom::regime {

namespace fal = ::foundation::algebra::lattices;

// Each atom names its tier as a member rather than encoding it in the
// name alone, so the self-test below can pin the family against
// HotPathTier instead of against a hand count.  A tier added to the
// lattice with no atom here then reddens this header.
struct hot final : atom_of<Axis::Regime> {
    static constexpr fal::HotPathTier tier = fal::HotPathTier::Hot;
};

struct warm final : atom_of<Axis::Regime> {
    static constexpr fal::HotPathTier tier = fal::HotPathTier::Warm;
};

struct cold final : atom_of<Axis::Regime> {
    static constexpr fal::HotPathTier tier = fal::HotPathTier::Cold;
};

}  // namespace fixy::atom::regime

namespace fixy::atom::detail {

using regime_atom_roster = std::tuple<regime::hot, regime::warm, regime::cold>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::regime_atom_self_test {

namespace fal = ::foundation::algebra::lattices;

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::regime, regime_atom_roster>(),
              "fixy/atoms/Regime.h: an atom declared in fixy::atom::regime is missing from "
              "regime_atom_roster.");

static_assert(every_roster_member_is_atom_<regime_atom_roster>(),
              "fixy/atoms/Regime.h: a member of regime_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<regime_atom_roster, Axis::Regime>(),
              "fixy/atoms/Regime.h: every regime atom engages Axis::Regime.");

// The pin: the family covers HotPathTier exactly.  A count would pass
// against three atoms all claiming Hot, so this asks each ENUMERATOR
// which atom claims it and requires exactly one.
// The pragma matches fixy/Atom.h's roster walks: an expansion
// statement's binding shadows itself across expansions, which -Wshadow
// reports once per expansion and which is not a finding.
template <fal::HotPathTier T>
[[nodiscard]] consteval std::size_t atoms_claiming_() noexcept {
    std::size_t claims = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : roster_members_v<regime_atom_roster>) {
        using A = [:member:];
        if constexpr (A::tier == T) ++claims;
    }
#pragma GCC diagnostic pop
    return claims;
}

[[nodiscard]] consteval bool every_tier_has_exactly_one_atom_() noexcept {
    bool exact = true;
    static constexpr auto tiers = std::define_static_array(std::meta::enumerators_of(^^fal::HotPathTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto tier_member : tiers) {
        constexpr fal::HotPathTier tier = [:tier_member:];
        exact = exact && (atoms_claiming_<tier>() == 1);
    }
#pragma GCC diagnostic pop
    return exact;
}

static_assert(every_tier_has_exactly_one_atom_(),
              "fixy/atoms/Regime.h: every HotPathTier enumerator must be claimed by exactly one atom in "
              "fixy::atom::regime.  A tier with no atom cannot be written by a caller, and a tier with two "
              "means one of them is unreachable.");

// The tier is part of the identity, so tier 4 refuses a pack that names
// two of them.  (fn's tier 4 is what does the refusing; these say the
// three types are distinct, which is what it reads.)
static_assert(!std::is_same_v<regime::hot, regime::warm>);
static_assert(!std::is_same_v<regime::hot, regime::cold>);
static_assert(!std::is_same_v<regime::warm, regime::cold>);

// No regime atom lifts.  The comment at the head of this file says why;
// this is the assertion that keeps the comment true.
static_assert(!::foundation::effects::LiftsToRow<regime::hot>);
static_assert(!::foundation::effects::LiftsToRow<regime::warm>);
static_assert(!::foundation::effects::LiftsToRow<regime::cold>);

}  // namespace fixy::atom::detail::regime_atom_self_test
