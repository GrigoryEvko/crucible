#pragma once

// Chain over Linux scheduling policies, ordered by how aggressively a
// thread in the class preempts the default time-shared pool.  bottom is
// Idle and top is Deadline.  leq(weak, strong) reads "a weaker
// requirement is satisfied by a stronger provider", so a FIFO thread
// serves a round-robin requirement.
//
// These ordinals are the preemption rank.  They are not the SCHED_*
// syscall constants, which run in a different order.  Code that reaches
// the sched_setattr boundary translates.
//
// SCHED_FIFO and its siblings are preprocessor macros, so the
// enumerators are spelled in PascalCase.  The kernel spellings appear
// only inside string literals, which the preprocessor leaves alone.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class SchedulerPolicy : std::uint8_t {
    Idle = 0,  // SCHED_IDLE — runs only when the CPU is otherwise idle
    Batch = 1,  // SCHED_BATCH — throughput-oriented, no interactive boost
    Other = 2,  // SCHED_OTHER — the default time-shared class
    RoundRobin = 3,  // SCHED_RR — real time, time-sliced among equal priorities
    Fifo = 4,  // SCHED_FIFO — real time, runs until it yields or blocks
    Deadline = 5,  // SCHED_DEADLINE — admitted earliest-deadline-first
};

inline constexpr std::size_t scheduler_policy_count = std::meta::enumerators_of(^^SchedulerPolicy).size();

[[nodiscard]] consteval std::string_view scheduler_policy_name(SchedulerPolicy p) noexcept {
    switch (p) {
        case SchedulerPolicy::Idle:
            return "Idle";
        case SchedulerPolicy::Batch:
            return "Batch";
        case SchedulerPolicy::Other:
            return "Other";
        case SchedulerPolicy::RoundRobin:
            return "RoundRobin";
        case SchedulerPolicy::Fifo:
            return "Fifo";
        case SchedulerPolicy::Deadline:
            return "Deadline";
        default:
            return std::string_view{"<unknown SchedulerPolicy>"};
    }
}

struct SchedulerPolicyLattice : ChainLatticeOps<SchedulerPolicy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return SchedulerPolicy::Idle; }
    [[nodiscard]] static constexpr element_type top() noexcept { return SchedulerPolicy::Deadline; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "SchedulerPolicyLattice"; }

    template <SchedulerPolicy P>
    struct At {
        struct element_type {
            using scheduler_policy_value_type = SchedulerPolicy;
            [[nodiscard]] constexpr operator scheduler_policy_value_type() const noexcept { return P; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr SchedulerPolicy policy = P;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (P) {
                case SchedulerPolicy::Idle:
                    return "SchedulerPolicyLattice::At<Idle>";
                case SchedulerPolicy::Batch:
                    return "SchedulerPolicyLattice::At<Batch>";
                case SchedulerPolicy::Other:
                    return "SchedulerPolicyLattice::At<Other>";
                case SchedulerPolicy::RoundRobin:
                    return "SchedulerPolicyLattice::At<RoundRobin>";
                case SchedulerPolicy::Fifo:
                    return "SchedulerPolicyLattice::At<Fifo>";
                case SchedulerPolicy::Deadline:
                    return "SchedulerPolicyLattice::At<Deadline>";
                default:
                    return "SchedulerPolicyLattice::At<?>";
            }
        }
    };
};

namespace scheduler_policy {
using IdleClass = SchedulerPolicyLattice::At<SchedulerPolicy::Idle>;
using BatchClass = SchedulerPolicyLattice::At<SchedulerPolicy::Batch>;
using OtherClass = SchedulerPolicyLattice::At<SchedulerPolicy::Other>;
using RoundRobinClass = SchedulerPolicyLattice::At<SchedulerPolicy::RoundRobin>;
using FifoClass = SchedulerPolicyLattice::At<SchedulerPolicy::Fifo>;
using DeadlineClass = SchedulerPolicyLattice::At<SchedulerPolicy::Deadline>;
}  // namespace scheduler_policy

namespace detail::scheduler_policy_lattice_self_test {

static_assert(scheduler_policy_count == 6, "SchedulerPolicy catalog diverged from {Idle, Batch, Other, "
                                           "RoundRobin, Fifo, Deadline}.  A new class needs both name "
                                           "switches extended and the admission thresholds that name a "
                                           "class rechecked.");

[[nodiscard]] consteval bool every_scheduler_policy_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SchedulerPolicy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (scheduler_policy_name([:en:]) == std::string_view{"<unknown SchedulerPolicy>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_scheduler_policy_has_name(), "scheduler_policy_name() switch missing an arm for at least one "
                                                 "class.  Add the arm or the new class leaks the '<unknown "
                                                 "SchedulerPolicy>' sentinel into diagnostic output.");

static_assert(Lattice<SchedulerPolicyLattice>);
static_assert(BoundedLattice<SchedulerPolicyLattice>);
static_assert(Lattice<scheduler_policy::IdleClass>);
static_assert(Lattice<scheduler_policy::DeadlineClass>);
static_assert(BoundedLattice<scheduler_policy::DeadlineClass>);

static_assert(!UnboundedLattice<SchedulerPolicyLattice>);
static_assert(!Semiring<SchedulerPolicyLattice>);

static_assert(std::is_empty_v<scheduler_policy::IdleClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::OtherClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::FifoClass::element_type>);
static_assert(std::is_empty_v<scheduler_policy::DeadlineClass::element_type>);

static_assert(verify_chain_lattice_exhaustive<SchedulerPolicyLattice>(),
              "SchedulerPolicyLattice chain-order lattice axioms fail at some "
              "triple.  The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<SchedulerPolicyLattice>(),
              "SchedulerPolicyLattice chain fails distributivity at some triple.  "
              "A chain order always satisfies it, so the defect is in join or "
              "meet.");

static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Idle, SchedulerPolicy::Batch));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Batch, SchedulerPolicy::Other));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo, SchedulerPolicy::Deadline));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Idle, SchedulerPolicy::Deadline));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo),
              "A FIFO thread serves a round-robin requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Fifo, SchedulerPolicy::RoundRobin),
              "A round-robin thread does not satisfy a FIFO requirement.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Deadline, SchedulerPolicy::Idle));

static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Other));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::RoundRobin));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Fifo));
static_assert(SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Deadline));
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Batch),
              "SCHED_BATCH sits below the Other threshold that a timestamp-counter "
              "read requires.  The kernel may migrate the thread mid-quantum, so a "
              "sched_getcpu pin proves nothing there.");
static_assert(!SchedulerPolicyLattice::leq(SchedulerPolicy::Other, SchedulerPolicy::Idle));

static_assert(SchedulerPolicyLattice::bottom() == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::top() == SchedulerPolicy::Deadline);

static_assert(SchedulerPolicyLattice::join(SchedulerPolicy::Idle, SchedulerPolicy::Deadline)
              == SchedulerPolicy::Deadline);
static_assert(SchedulerPolicyLattice::join(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)
              == SchedulerPolicy::Fifo);
static_assert(SchedulerPolicyLattice::meet(SchedulerPolicy::Idle, SchedulerPolicy::Deadline) == SchedulerPolicy::Idle);
static_assert(SchedulerPolicyLattice::meet(SchedulerPolicy::RoundRobin, SchedulerPolicy::Fifo)
              == SchedulerPolicy::RoundRobin);

static_assert(SchedulerPolicyLattice::name() == "SchedulerPolicyLattice");
static_assert(scheduler_policy::IdleClass::name() == "SchedulerPolicyLattice::At<Idle>");
static_assert(scheduler_policy::BatchClass::name() == "SchedulerPolicyLattice::At<Batch>");
static_assert(scheduler_policy::OtherClass::name() == "SchedulerPolicyLattice::At<Other>");
static_assert(scheduler_policy::RoundRobinClass::name() == "SchedulerPolicyLattice::At<RoundRobin>");
static_assert(scheduler_policy::FifoClass::name() == "SchedulerPolicyLattice::At<Fifo>");
static_assert(scheduler_policy::DeadlineClass::name() == "SchedulerPolicyLattice::At<Deadline>");

[[nodiscard]] consteval bool every_at_scheduler_policy_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SchedulerPolicy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (SchedulerPolicyLattice::At<([:en:])>::name() == std::string_view{"SchedulerPolicyLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_scheduler_policy_has_name(),
              "SchedulerPolicyLattice::At<P>::name() switch missing an arm for at "
              "least one class.  Add the arm or the new class leaks the "
              "'SchedulerPolicyLattice::At<?>' sentinel.");

static_assert(scheduler_policy::IdleClass::policy == SchedulerPolicy::Idle);
static_assert(scheduler_policy::BatchClass::policy == SchedulerPolicy::Batch);
static_assert(scheduler_policy::OtherClass::policy == SchedulerPolicy::Other);
static_assert(scheduler_policy::RoundRobinClass::policy == SchedulerPolicy::RoundRobin);
static_assert(scheduler_policy::FifoClass::policy == SchedulerPolicy::Fifo);
static_assert(scheduler_policy::DeadlineClass::policy == SchedulerPolicy::Deadline);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using FifoGraded = Graded<ModalityKind::Absolute, scheduler_policy::FifoClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FifoGraded, int);

template <typename T_>
using DeadlineGraded = Graded<ModalityKind::Absolute, scheduler_policy::DeadlineClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(DeadlineGraded, EightByteValue);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    SchedulerPolicy a = SchedulerPolicy::Idle;
    SchedulerPolicy b = SchedulerPolicy::Deadline;
    [[maybe_unused]] bool l1 = SchedulerPolicyLattice::leq(a, b);
    [[maybe_unused]] SchedulerPolicy j1 = SchedulerPolicyLattice::join(a, b);
    [[maybe_unused]] SchedulerPolicy m1 = SchedulerPolicyLattice::meet(a, b);
    [[maybe_unused]] SchedulerPolicy bot = SchedulerPolicyLattice::bottom();
    [[maybe_unused]] SchedulerPolicy top = SchedulerPolicyLattice::top();

    SchedulerPolicy rr = SchedulerPolicy::RoundRobin;
    SchedulerPolicy fifo = SchedulerPolicy::Fifo;
    SchedulerPolicy other = SchedulerPolicy::Other;
    [[maybe_unused]] SchedulerPolicy j2 = SchedulerPolicyLattice::join(rr, fifo);
    [[maybe_unused]] SchedulerPolicy m2 = SchedulerPolicyLattice::meet(rr, fifo);
    [[maybe_unused]] bool tsc_ok = SchedulerPolicyLattice::leq(other, fifo);

    OneByteValue v{42};
    FifoGraded<OneByteValue> initial{v, scheduler_policy::FifoClass::bottom()};
    auto widened = initial.weaken(scheduler_policy::FifoClass::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(scheduler_policy::FifoClass::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    scheduler_policy::FifoClass::element_type e{};
    [[maybe_unused]] SchedulerPolicy rec = e;
}

}  // namespace detail::scheduler_policy_lattice_self_test

}  // namespace crucible::algebra::lattices
