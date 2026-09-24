// The enumerator names match the scheduler classes the kernel exposes,
// but the ordinals do not.  This order is a preemption rank of the
// lattice's own devising, and an ordinal here is never the value of the
// matching syscall constant.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_SchedulerPolicyLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace sp = ::crucible::algebra::lattices::scheduler_policy;

namespace {

using cal::SchedulerPolicy;
using L = cal::SchedulerPolicyLattice;

static_assert(crucible::algebra::Lattice<L>, "SchedulerPolicyLattice must satisfy the Lattice concept "
                                             "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>, "SchedulerPolicyLattice has both bottom() (Idle) and top() "
                                                    "(Deadline) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>, "SchedulerPolicyLattice is not a semiring — it carries no "
                                               "equality+add+mul algebra, only the order-theoretic operations.");

static_assert(cal::scheduler_policy_count == 6, "SchedulerPolicy must have exactly 6 enumerators {Idle, Batch, "
                                                "Other, RoundRobin, Fifo, Deadline}.  A new one must take the "
                                                "ordinal that keeps integer order equal to preemption "
                                                "aggressiveness, and must be added to both name switches.");

static_assert(std::is_same_v<std::underlying_type_t<SchedulerPolicy>, std::uint8_t>,
              "SchedulerPolicy must use uint8_t as its underlying type — the "
              "ordinal is the preemption rank, from Idle at 0 to Deadline at 5, "
              "and never the value of the matching syscall constant.");

static_assert(std::to_underlying(SchedulerPolicy::Idle) == 0);
static_assert(std::to_underlying(SchedulerPolicy::Batch) == 1);
static_assert(std::to_underlying(SchedulerPolicy::Other) == 2);
static_assert(std::to_underlying(SchedulerPolicy::RoundRobin) == 3);
static_assert(std::to_underlying(SchedulerPolicy::Fifo) == 4);
static_assert(std::to_underlying(SchedulerPolicy::Deadline) == 5);

static_assert(L::bottom() == SchedulerPolicy::Idle);
static_assert(L::top() == SchedulerPolicy::Deadline);

static_assert(L::leq(SchedulerPolicy::Idle, SchedulerPolicy::Batch));
static_assert(L::leq(SchedulerPolicy::Batch, SchedulerPolicy::Other));
static_assert(L::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert(L::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo));
static_assert(L::leq(SchedulerPolicy::Fifo, SchedulerPolicy::Deadline));
static_assert(L::leq(SchedulerPolicy::Idle, SchedulerPolicy::Deadline), "transitive endpoints");

// The chain must not run downwards.
static_assert(!L::leq(SchedulerPolicy::Fifo, SchedulerPolicy::RoundRobin),
              "RoundRobin ⋣ Fifo — a round-robin thread does not satisfy a FIFO "
              "requirement.");
static_assert(!L::leq(SchedulerPolicy::Deadline, SchedulerPolicy::Fifo));
static_assert(!L::leq(SchedulerPolicy::Other, SchedulerPolicy::Idle));
static_assert(!L::leq(SchedulerPolicy::Batch, SchedulerPolicy::Idle));

// Reading the timestamp counter needs at least the Other class, so the
// threshold admits it and everything above it and nothing below.
static_assert(L::leq(SchedulerPolicy::Other, SchedulerPolicy::Other));
static_assert(L::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert(L::leq(SchedulerPolicy::Other, SchedulerPolicy::Fifo));
static_assert(L::leq(SchedulerPolicy::Other, SchedulerPolicy::Deadline));
static_assert(!L::leq(SchedulerPolicy::Other, SchedulerPolicy::Batch),
              "The batch class sits below the threshold for reading the "
              "timestamp counter.");
static_assert(!L::leq(SchedulerPolicy::Other, SchedulerPolicy::Idle));

// Join strengthens and meet weakens.
static_assert(L::join(SchedulerPolicy::Idle, SchedulerPolicy::Deadline) == SchedulerPolicy::Deadline);
static_assert(L::join(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo) == SchedulerPolicy::Fifo);
static_assert(L::join(SchedulerPolicy::Idle, SchedulerPolicy::Batch) == SchedulerPolicy::Batch,
              "Idle is the join identity");
static_assert(L::meet(SchedulerPolicy::Idle, SchedulerPolicy::Deadline) == SchedulerPolicy::Idle,
              "Idle absorbs in meet");
static_assert(L::meet(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo) == SchedulerPolicy::RoundRobin);
static_assert(L::meet(SchedulerPolicy::Deadline, SchedulerPolicy::Deadline) == SchedulerPolicy::Deadline);

static_assert(crucible::algebra::Lattice<sp::IdleClass>);
static_assert(crucible::algebra::Lattice<sp::FifoClass>);
static_assert(crucible::algebra::BoundedLattice<sp::DeadlineClass>);
static_assert(std::is_empty_v<sp::IdleClass::element_type>,
              "At<Idle>::element_type must be empty so that Graded<Absolute, "
              "At<Idle>, P> collapses to sizeof(P) — a zero-byte scheduler-class "
              "annotation at every binding site.");
static_assert(std::is_empty_v<sp::OtherClass::element_type>);
static_assert(std::is_empty_v<sp::FifoClass::element_type>);
static_assert(std::is_empty_v<sp::DeadlineClass::element_type>);
static_assert(sp::FifoClass::policy == SchedulerPolicy::Fifo,
              "At<P>::policy must equal P at the type level, so a wrapper can "
              "read the pinned policy with no runtime data.");
static_assert(sp::RoundRobinClass::policy == SchedulerPolicy::RoundRobin);
static_assert(sp::IdleClass::policy == SchedulerPolicy::Idle);
static_assert(sp::DeadlineClass::policy == SchedulerPolicy::Deadline);

struct EightByteValue {
    unsigned long long v{0};
};
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, sp::FifoClass, EightByteValue>)
        == sizeof(EightByteValue),
    "Pinning a Fifo grade must add zero bytes to an 8-byte payload.");
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, sp::IdleClass, int>)
              == sizeof(int));

static_assert(L::name() == std::string_view{"SchedulerPolicyLattice"});
static_assert(sp::FifoClass::name() == std::string_view{"SchedulerPolicyLattice::At<Fifo>"});
static_assert(sp::RoundRobinClass::name() == std::string_view{"SchedulerPolicyLattice::At<RoundRobin>"});
static_assert(sp::DeadlineClass::name() == std::string_view{"SchedulerPolicyLattice::At<Deadline>"});
static_assert(sp::IdleClass::name() == std::string_view{"SchedulerPolicyLattice::At<Idle>"});
static_assert(cal::scheduler_policy_name(SchedulerPolicy::RoundRobin) == std::string_view{"RoundRobin"});

}  // namespace

int main() {
    cal::detail::scheduler_policy_lattice_self_test::runtime_smoke_test();
    return 0;
}
