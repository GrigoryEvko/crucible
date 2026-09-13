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

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class SuspendBehavior : std::uint8_t {
    Unknown = 0,  // undeclared
    PausesOnSuspend = 1,  // CLOCK_MONOTONIC — stops while the system is suspended
    KeepsTicking = 2,  // CLOCK_BOOTTIME — advances through suspend
};

inline constexpr std::size_t suspend_behavior_count = std::meta::enumerators_of(^^SuspendBehavior).size();

[[nodiscard]] consteval std::string_view suspend_behavior_name(SuspendBehavior b) noexcept {
    switch (b) {
        case SuspendBehavior::Unknown:
            return "Unknown";
        case SuspendBehavior::PausesOnSuspend:
            return "PausesOnSuspend";
        case SuspendBehavior::KeepsTicking:
            return "KeepsTicking";
        default:
            return std::string_view{"<unknown SuspendBehavior>"};
    }
}

struct SuspendBehaviorLattice : ChainLatticeOps<SuspendBehavior> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return SuspendBehavior::Unknown; }
    [[nodiscard]] static constexpr element_type top() noexcept { return SuspendBehavior::KeepsTicking; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "SuspendBehaviorLattice"; }

    template <SuspendBehavior B>
    struct At {
        struct element_type {
            using suspend_behavior_value_type = SuspendBehavior;
            [[nodiscard]] constexpr operator suspend_behavior_value_type() const noexcept { return B; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr SuspendBehavior behavior = B;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (B) {
                case SuspendBehavior::Unknown:
                    return "SuspendBehaviorLattice::At<Unknown>";
                case SuspendBehavior::PausesOnSuspend:
                    return "SuspendBehaviorLattice::At<PausesOnSuspend>";
                case SuspendBehavior::KeepsTicking:
                    return "SuspendBehaviorLattice::At<KeepsTicking>";
                default:
                    return "SuspendBehaviorLattice::At<?>";
            }
        }
    };
};

namespace suspend_behavior {
using UnknownBehavior = SuspendBehaviorLattice::At<SuspendBehavior::Unknown>;
using PausesOnSuspendClock = SuspendBehaviorLattice::At<SuspendBehavior::PausesOnSuspend>;
using KeepsTickingClock = SuspendBehaviorLattice::At<SuspendBehavior::KeepsTicking>;
}  // namespace suspend_behavior

namespace detail::suspend_behavior_lattice_self_test {

static_assert(suspend_behavior_count == 3, "SuspendBehavior catalog diverged from {Unknown, PausesOnSuspend, "
                                           "KeepsTicking}.  A new behavior needs both name switches extended "
                                           "and every composite that names a behavior rechecked.");

[[nodiscard]] consteval bool every_suspend_behavior_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SuspendBehavior));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (suspend_behavior_name([:en:]) == std::string_view{"<unknown SuspendBehavior>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_suspend_behavior_has_name(), "suspend_behavior_name() switch missing an arm for at least one "
                                                 "behavior.  Add the arm or the new behavior leaks the "
                                                 "'<unknown SuspendBehavior>' sentinel into diagnostic output.");

static_assert(Lattice<SuspendBehaviorLattice>);
static_assert(BoundedLattice<SuspendBehaviorLattice>);
static_assert(Lattice<suspend_behavior::UnknownBehavior>);
static_assert(Lattice<suspend_behavior::KeepsTickingClock>);
static_assert(BoundedLattice<suspend_behavior::KeepsTickingClock>);

static_assert(!UnboundedLattice<SuspendBehaviorLattice>);
static_assert(!Semiring<SuspendBehaviorLattice>);

static_assert(std::is_empty_v<suspend_behavior::UnknownBehavior::element_type>);
static_assert(std::is_empty_v<suspend_behavior::PausesOnSuspendClock::element_type>);
static_assert(std::is_empty_v<suspend_behavior::KeepsTickingClock::element_type>);

static_assert(verify_chain_lattice_exhaustive<SuspendBehaviorLattice>(),
              "SuspendBehaviorLattice chain-order lattice axioms fail at some "
              "triple.  The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<SuspendBehaviorLattice>(),
              "SuspendBehaviorLattice chain fails distributivity at some triple.  "
              "A chain order always satisfies it, so the defect is in join or "
              "meet.");

static_assert(SuspendBehaviorLattice::leq(SuspendBehavior::Unknown, SuspendBehavior::PausesOnSuspend));
static_assert(SuspendBehaviorLattice::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking));
static_assert(SuspendBehaviorLattice::leq(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking));
static_assert(SuspendBehaviorLattice::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking),
              "A boot-clock provider serves a monotonic-clock requirement.");
static_assert(!SuspendBehaviorLattice::leq(SuspendBehavior::KeepsTicking, SuspendBehavior::PausesOnSuspend),
              "A monotonic clock does not satisfy a suspend-inclusive "
              "requirement.  That pairing is the false-healthy deadline reading "
              "this axis forbids.");
static_assert(!SuspendBehaviorLattice::leq(SuspendBehavior::KeepsTicking, SuspendBehavior::Unknown));

static_assert(SuspendBehaviorLattice::bottom() == SuspendBehavior::Unknown);
static_assert(SuspendBehaviorLattice::top() == SuspendBehavior::KeepsTicking);

static_assert(SuspendBehaviorLattice::join(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::KeepsTicking);
static_assert(SuspendBehaviorLattice::join(SuspendBehavior::PausesOnSuspend, SuspendBehavior::Unknown)
              == SuspendBehavior::PausesOnSuspend);
static_assert(SuspendBehaviorLattice::meet(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::Unknown);
static_assert(SuspendBehaviorLattice::meet(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::PausesOnSuspend);

static_assert(SuspendBehaviorLattice::name() == "SuspendBehaviorLattice");
static_assert(suspend_behavior::UnknownBehavior::name() == "SuspendBehaviorLattice::At<Unknown>");
static_assert(suspend_behavior::PausesOnSuspendClock::name() == "SuspendBehaviorLattice::At<PausesOnSuspend>");
static_assert(suspend_behavior::KeepsTickingClock::name() == "SuspendBehaviorLattice::At<KeepsTicking>");

[[nodiscard]] consteval bool every_at_suspend_behavior_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SuspendBehavior));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (SuspendBehaviorLattice::At<([:en:])>::name() == std::string_view{"SuspendBehaviorLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_suspend_behavior_has_name(),
              "SuspendBehaviorLattice::At<B>::name() switch missing an arm for at "
              "least one behavior.  Add the arm or the new behavior leaks the "
              "'SuspendBehaviorLattice::At<?>' sentinel.");

static_assert(suspend_behavior::UnknownBehavior::behavior == SuspendBehavior::Unknown);
static_assert(suspend_behavior::PausesOnSuspendClock::behavior == SuspendBehavior::PausesOnSuspend);
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

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    SuspendBehavior a = SuspendBehavior::Unknown;
    SuspendBehavior b = SuspendBehavior::KeepsTicking;
    [[maybe_unused]] bool l1 = SuspendBehaviorLattice::leq(a, b);
    [[maybe_unused]] SuspendBehavior j1 = SuspendBehaviorLattice::join(a, b);
    [[maybe_unused]] SuspendBehavior m1 = SuspendBehaviorLattice::meet(a, b);
    [[maybe_unused]] SuspendBehavior bot = SuspendBehaviorLattice::bottom();
    [[maybe_unused]] SuspendBehavior top = SuspendBehaviorLattice::top();

    SuspendBehavior mono = SuspendBehavior::PausesOnSuspend;
    SuspendBehavior boot = SuspendBehavior::KeepsTicking;
    [[maybe_unused]] SuspendBehavior j2 = SuspendBehaviorLattice::join(mono, boot);
    [[maybe_unused]] SuspendBehavior m2 = SuspendBehaviorLattice::meet(mono, boot);

    OneByteValue v{42};
    BootClockGraded<OneByteValue> initial{v, suspend_behavior::KeepsTickingClock::bottom()};
    auto widened = initial.weaken(suspend_behavior::KeepsTickingClock::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(suspend_behavior::KeepsTickingClock::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    suspend_behavior::KeepsTickingClock::element_type e{};
    [[maybe_unused]] SuspendBehavior rec = e;
}

}  // namespace detail::suspend_behavior_lattice_self_test

}  // namespace crucible::algebra::lattices
