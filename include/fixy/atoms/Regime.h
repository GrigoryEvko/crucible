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
// unbounded cost, or an allocation or an I/O call, or a Bg row, or
// buffered stdio, or a coroutine frame, is refused by fixy/Collision.h's
// H and R and S families before a bench ever runs.  The measurement
// settles the cases that survive the contradictions, not the
// contradictions themselves.
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

#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::atom::regime {

inline constexpr atom_seal atom_namespace_seal{};

namespace fal = ::foundation::algebra::lattices;

// Each atom names its tier as a member rather than encoding it in the
// name alone, so the check file of this header can pin the family
// against HotPathTier instead of against a hand count.  A tier added to
// the lattice with no atom here then stops the build of that check file.
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
