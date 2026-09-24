#pragma once

// Chain over what a clock does across a system suspend.  bottom is
// Unknown and top is KeepsTicking.  A clock that keeps advancing through
// suspend also answers every within-run interval question, so it sits
// above one that pauses, and leq(weak, strong) reads "a weaker
// requirement is satisfied by a stronger provider".
//
// Unknown is the absence of a claim, not a third behavior.
//
// A deadline measured on a pausing clock under-reports elapsed real time
// across a suspend window, so a watchdog reading it stays quiet when it
// should fire.  A consumer that needs suspend-inclusive elapsed declares
// KeepsTicking, and the order then rejects anything below it.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class SuspendBehavior : std::uint8_t {
    Unknown = 0,  // undeclared
    PausesOnSuspend = 1,  // CLOCK_MONOTONIC — stops while the system is suspended
    KeepsTicking = 2,  // CLOCK_BOOTTIME — advances through suspend
};

inline constexpr std::size_t suspend_behavior_count = ::foundation::reflect::enum_count<SuspendBehavior>;

// The identifier of b, or "<unknown SuspendBehavior>" for a value
// outside the enum.
[[nodiscard]] consteval std::string_view suspend_behavior_name(SuspendBehavior b) noexcept {
    return ::foundation::reflect::enum_name(b);
}

struct SuspendBehaviorLattice : ChainLatticeOps<SuspendBehavior> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return SuspendBehavior::Unknown; }
    [[nodiscard]] static constexpr element_type top() noexcept { return SuspendBehavior::KeepsTicking; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "SuspendBehaviorLattice"; }

    template <SuspendBehavior B>
    struct AtElement : PinnedElement<B> {
        using suspend_behavior_value_type = SuspendBehavior;
    };

    template <SuspendBehavior B>
    struct At : PinnedAt<SuspendBehaviorLattice, B, AtElement<B>> {
        static constexpr SuspendBehavior behavior = B;
    };
};

namespace suspend_behavior {
using UnknownBehavior = SuspendBehaviorLattice::At<SuspendBehavior::Unknown>;
using PausesOnSuspendClock = SuspendBehaviorLattice::At<SuspendBehavior::PausesOnSuspend>;
using KeepsTickingClock = SuspendBehaviorLattice::At<SuspendBehavior::KeepsTicking>;
}  // namespace suspend_behavior

namespace detail::suspend_behavior_lattice_self_test {

static_assert(suspend_behavior_count == 3, "SuspendBehavior catalog diverged from {Unknown, PausesOnSuspend, "
                                           "KeepsTicking}.  A new behavior needs every composite that names "
                                           "a behavior rechecked.");

static_assert(verify_chain_lattice<SuspendBehaviorLattice>(),
              "SuspendBehaviorLattice: the chain order, the pinned grades or the "
              "reflected names diverged from the SuspendBehavior enumerator list.");

static_assert(!UnboundedLattice<SuspendBehaviorLattice>);
static_assert(!Semiring<SuspendBehaviorLattice>);

static_assert(SuspendBehaviorLattice::bottom() == SuspendBehavior::Unknown);
static_assert(SuspendBehaviorLattice::top() == SuspendBehavior::KeepsTicking);

static_assert(SuspendBehaviorLattice::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking),
              "A boot-clock provider serves a monotonic-clock requirement.");
static_assert(!SuspendBehaviorLattice::leq(SuspendBehavior::KeepsTicking, SuspendBehavior::PausesOnSuspend),
              "A monotonic clock does not satisfy a suspend-inclusive "
              "requirement.  That pairing is the false-healthy deadline reading "
              "this axis forbids.");

static_assert(SuspendBehaviorLattice::name() == "SuspendBehaviorLattice");
static_assert(suspend_behavior::UnknownBehavior::name() == "SuspendBehaviorLattice::At<Unknown>");
static_assert(suspend_behavior::KeepsTickingClock::name() == "SuspendBehaviorLattice::At<KeepsTicking>");
static_assert(SuspendBehaviorLattice::At<static_cast<SuspendBehavior>(255)>::name() == "SuspendBehaviorLattice::At<?>");

static_assert(suspend_behavior_name(SuspendBehavior::PausesOnSuspend) == "PausesOnSuspend");
static_assert(suspend_behavior_name(static_cast<SuspendBehavior>(255)) == "<unknown SuspendBehavior>");

static_assert(suspend_behavior::UnknownBehavior::behavior == SuspendBehavior::Unknown);
static_assert(suspend_behavior::KeepsTickingClock::behavior == SuspendBehavior::KeepsTicking);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using BootClockGraded = Graded<ModalityKind::Absolute, suspend_behavior::KeepsTickingClock, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockGraded, int);

template <typename T_>
using MonoClockGraded = Graded<ModalityKind::Absolute, suspend_behavior::PausesOnSuspendClock, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MonoClockGraded, EightByteValue);

}  // namespace detail::suspend_behavior_lattice_self_test

}  // namespace foundation::algebra::lattices
