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

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

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

// A clock that does not stop across a suspend is the stronger claim.
struct SuspendBehaviorLattice
    : EnumChainLattice<SuspendBehaviorLattice, SuspendBehavior, ClaimOrientation::stronger_is_higher> {
    template <SuspendBehavior B>
    struct At : PinnedAt<SuspendBehaviorLattice, B> {
        static constexpr SuspendBehavior behavior = B;
    };
};

namespace suspend_behavior {
using UnknownBehavior = SuspendBehaviorLattice::At<SuspendBehavior::Unknown>;
using PausesOnSuspendClock = SuspendBehaviorLattice::At<SuspendBehavior::PausesOnSuspend>;
using KeepsTickingClock = SuspendBehaviorLattice::At<SuspendBehavior::KeepsTicking>;
}  // namespace suspend_behavior

}  // namespace foundation::algebra::lattices
