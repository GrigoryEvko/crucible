#pragma once

// The synchronization atoms.  Every atom here engages
// Axis::Synchronization.
//
// Synchronization is HOW a body waits for a cross-thread event.  It is
// not Reentrancy: Reentrancy tracks call-graph self-call, and a waiting
// strategy says nothing about self-call.  fixy/Axis.h says so at the
// enumerator.
//
// ---------------------------------------------------------------------
// The ladder, and the one line that divides it
//
// The six grades come from foundation's WaitLattice, whose own header
// draws the division these atoms turn into a gate: the three lowest
// enter the kernel or the scheduler, the three highest stay in user
// space.  That line is the whole content of this axis for the collision
// rules, and it is also where the lift changes.
//
// The cost hierarchy is CLAUDE.md IX's, measured rather than asserted:
// a spin on an acquire load costs 10-40 ns intra-socket because it is
// bounded by the cache-coherence fabric, and a futex wait costs 1-5 us
// because it is bounded by the scheduler.  Two orders of magnitude is
// what makes this axis worth typing.
//
// ---------------------------------------------------------------------
// The lift, and why this axis has one where Regime does not
//
// A kernel wait IS an operation: the body reaches the scheduler and may
// not come back promptly, which is exactly Effect::Block.  So the three
// lowest atoms lift to Row<Block> and a context whose row does not admit
// Block cannot host them.  The three highest lift to the empty row: a
// spin performs no operation the context has to permit, it only burns
// cycles.
//
// That is the difference from fixy/atoms/Regime.h, which lifts nothing.
// Regime declares a BUDGET, and a budget forbids operations rather than
// naming one.  This axis names one.
//
// The empty-row lift is not the same as no lift.  An atom with no
// `lifts_to` is invisible to foundation/effects/Lift.h and to every gate
// built on it; an atom that lifts to Row<> is visible and says "nothing
// required".  The spin atoms want the second, so that a context gate
// folding a pack's rows sees them and admits them, rather than skipping
// them and admitting by omission.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::atom::sync {

inline constexpr atom_seal atom_namespace_seal{};

namespace fal = ::foundation::algebra::lattices;
namespace fe = ::foundation::effects;

// ── The three that reach the kernel or the scheduler ─────────────────

struct block final : atom_of<Axis::Synchronization> {
    static constexpr fal::WaitStrategy strategy = fal::WaitStrategy::Block;
    static constexpr auto lifts_to = fe::Row<fe::Effect::Block>{};
};

struct park final : atom_of<Axis::Synchronization> {
    static constexpr fal::WaitStrategy strategy = fal::WaitStrategy::Park;
    static constexpr auto lifts_to = fe::Row<fe::Effect::Block>{};
};

struct acquire_wait final : atom_of<Axis::Synchronization> {
    static constexpr fal::WaitStrategy strategy = fal::WaitStrategy::AcquireWait;
    static constexpr auto lifts_to = fe::Row<fe::Effect::Block>{};
};

// ── The three that stay in user space ────────────────────────────────

// UMWAIT halts the core in C0.1 or C0.2 rather than spinning it, so it
// is a user-space wait that does NOT burn the core.  That is why W002
// below refuses the two spins and not this one.
struct umwait_c01 final : atom_of<Axis::Synchronization> {
    static constexpr fal::WaitStrategy strategy = fal::WaitStrategy::UmwaitC01;
    static constexpr auto lifts_to = fe::Row<>{};
};

struct bounded_spin final : atom_of<Axis::Synchronization> {
    static constexpr fal::WaitStrategy strategy = fal::WaitStrategy::BoundedSpin;
    static constexpr auto lifts_to = fe::Row<>{};
};

struct spin_pause final : atom_of<Axis::Synchronization> {
    static constexpr fal::WaitStrategy strategy = fal::WaitStrategy::SpinPause;
    static constexpr auto lifts_to = fe::Row<>{};
};

// Whether a strategy reaches the kernel or the scheduler.  The division
// is the WaitLattice's own, stated once here so the collision rules and
// the lift cannot disagree about where the line falls.
[[nodiscard]] consteval bool enters_the_kernel(fal::WaitStrategy strategy) noexcept {
    return strategy == fal::WaitStrategy::Block || strategy == fal::WaitStrategy::Park
        || strategy == fal::WaitStrategy::AcquireWait;
}

// Whether a strategy burns the core while it waits.  UMWAIT is the grade
// this excludes and enters_the_kernel does not mention, which is why the
// two predicates are separate rather than one negating the other.
[[nodiscard]] consteval bool burns_the_core(fal::WaitStrategy strategy) noexcept {
    return strategy == fal::WaitStrategy::BoundedSpin || strategy == fal::WaitStrategy::SpinPause;
}

}  // namespace fixy::atom::sync

namespace fixy::atom::detail {

using sync_atom_roster =
    std::tuple<sync::block, sync::park, sync::acquire_wait, sync::umwait_c01, sync::bounded_spin, sync::spin_pause>;

}  // namespace fixy::atom::detail
