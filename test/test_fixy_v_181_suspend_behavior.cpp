#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace sb = ::crucible::algebra::lattices::suspend_behavior;

namespace {

using cal::SuspendBehavior;
using L = cal::SuspendBehaviorLattice;

static_assert(crucible::algebra::Lattice<L>, "SuspendBehaviorLattice must satisfy the Lattice concept "
                                             "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>, "SuspendBehaviorLattice has both bottom() (Unknown) and top() "
                                                    "(KeepsTicking) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>, "SuspendBehaviorLattice is not a semiring — it carries no "
                                               "equality+add+mul algebra, only the order-theoretic operations.");

static_assert(cal::suspend_behavior_count == 3, "SuspendBehavior must have exactly 3 enumerators {Unknown, "
                                                "PausesOnSuspend, KeepsTicking}.  A new one must take the ordinal "
                                                "that keeps integer order equal to suspend resilience, and must be "
                                                "added to both name switches.");

static_assert(std::is_same_v<std::underlying_type_t<SuspendBehavior>, std::uint8_t>,
              "SuspendBehavior must use uint8_t as its underlying type — the "
              "ordinal is the suspend-resilience rank, from Unknown at 0 to "
              "KeepsTicking at 2.");

static_assert(std::to_underlying(SuspendBehavior::Unknown) == 0);
static_assert(std::to_underlying(SuspendBehavior::PausesOnSuspend) == 1);
static_assert(std::to_underlying(SuspendBehavior::KeepsTicking) == 2);

static_assert(L::bottom() == SuspendBehavior::Unknown);
static_assert(L::top() == SuspendBehavior::KeepsTicking);

static_assert(L::leq(SuspendBehavior::Unknown, SuspendBehavior::PausesOnSuspend));
static_assert(L::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking));
static_assert(L::leq(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking), "transitive endpoints");

// The chain must not run downwards.  A clock that pauses across suspend
// must never satisfy a requirement for elapsed time that includes it.
// Letting it through is what reports a deadline as healthy after the
// machine wakes up.
static_assert(!L::leq(SuspendBehavior::KeepsTicking, SuspendBehavior::PausesOnSuspend),
              "PausesOnSuspend ⋣ KeepsTicking — a clock that pauses across "
              "suspend does not satisfy a suspend-inclusive requirement.");
static_assert(!L::leq(SuspendBehavior::KeepsTicking, SuspendBehavior::Unknown));
static_assert(!L::leq(SuspendBehavior::PausesOnSuspend, SuspendBehavior::Unknown));

// Join takes the more suspend-resilient behaviour, meet the less.
static_assert(L::join(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking) == SuspendBehavior::KeepsTicking);
static_assert(L::join(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::KeepsTicking);
static_assert(L::join(SuspendBehavior::Unknown, SuspendBehavior::PausesOnSuspend) == SuspendBehavior::PausesOnSuspend,
              "Unknown is the join identity");
static_assert(L::join(SuspendBehavior::KeepsTicking, SuspendBehavior::Unknown) == SuspendBehavior::KeepsTicking,
              "KeepsTicking absorbs in join");

static_assert(L::meet(SuspendBehavior::Unknown, SuspendBehavior::KeepsTicking) == SuspendBehavior::Unknown,
              "Unknown absorbs in meet");
static_assert(L::meet(SuspendBehavior::PausesOnSuspend, SuspendBehavior::KeepsTicking)
              == SuspendBehavior::PausesOnSuspend);
static_assert(L::meet(SuspendBehavior::KeepsTicking, SuspendBehavior::KeepsTicking) == SuspendBehavior::KeepsTicking,
              "KeepsTicking is the meet identity at the top");

static_assert(crucible::algebra::Lattice<sb::UnknownBehavior>);
static_assert(crucible::algebra::Lattice<sb::PausesOnSuspendClock>);
static_assert(crucible::algebra::BoundedLattice<sb::KeepsTickingClock>);
static_assert(std::is_empty_v<sb::UnknownBehavior::element_type>,
              "At<Unknown>::element_type must be empty so that Graded<Absolute, "
              "At<Unknown>, P> collapses to sizeof(P) — a zero-byte "
              "suspend-behavior annotation at every binding site.");
static_assert(std::is_empty_v<sb::PausesOnSuspendClock::element_type>);
static_assert(std::is_empty_v<sb::KeepsTickingClock::element_type>);
static_assert(sb::KeepsTickingClock::behavior == SuspendBehavior::KeepsTicking,
              "At<B>::behavior must equal B at the type level, so a wrapper can "
              "read the pinned behavior with no runtime data.");
static_assert(sb::PausesOnSuspendClock::behavior == SuspendBehavior::PausesOnSuspend);
static_assert(sb::UnknownBehavior::behavior == SuspendBehavior::Unknown);

struct EightByteValue {
    unsigned long long v{0};
};
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, sb::KeepsTickingClock, EightByteValue>)
        == sizeof(EightByteValue),
    "Pinning a KeepsTicking grade must add zero bytes to an 8-byte "
    "payload.");
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, sb::UnknownBehavior, int>)
              == sizeof(int));

static_assert(L::name() == std::string_view{"SuspendBehaviorLattice"});
static_assert(sb::KeepsTickingClock::name() == std::string_view{"SuspendBehaviorLattice::At<KeepsTicking>"});
static_assert(sb::PausesOnSuspendClock::name() == std::string_view{"SuspendBehaviorLattice::At<PausesOnSuspend>"});
static_assert(sb::UnknownBehavior::name() == std::string_view{"SuspendBehaviorLattice::At<Unknown>"});
static_assert(cal::suspend_behavior_name(SuspendBehavior::KeepsTicking) == std::string_view{"KeepsTicking"});

}  // namespace

int main() {
    cal::detail::suspend_behavior_lattice_self_test::runtime_smoke_test();
    return 0;
}
