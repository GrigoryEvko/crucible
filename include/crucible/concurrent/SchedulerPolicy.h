#pragma once

// The vocabulary of thread-pool dispatch policies.  A policy is a tag
// type carrying its own metadata, chosen at compile time, so no
// dispatch table or virtual call survives into the pool.  This header
// ships the vocabulary alone: the pool that consumes a tag lives
// elsewhere, and separating them lets configuration, tests and benches
// name a policy without one.
//
// LocalityAware is the default because the primary workload is
// fork-join over short-lived tasks on a contiguous arena, where cache
// locality decides throughput.  Against it: the fair-share and deadline
// policies each spend more per submit than such a task costs to run, a
// single shared queue turns its head into a cache cliff once the worker
// count grows, and round-robin balances the load but has nothing to say
// about topology.
//
// A new policy inherits from the base tag and supplies the same
// metadata fields.  Detection is by inheritance, so nothing else has to
// be specialized for it.

#include <crucible/Platform.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::concurrent::scheduler {

struct policy_base {};

struct Fifo : policy_base {
    static constexpr std::string_view name = "Fifo";
    static constexpr std::string_view description = "One shared MPMC queue; strict global FIFO dispatch order.";
    static constexpr std::string_view use_case = "Ordered processing, simple debugging, strong FIFO invariants.";
    static constexpr std::size_t typical_submit_ns = 20;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = false;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = false;
    static constexpr bool provides_bounded_latency = false;
};

struct Lifo : policy_base {
    static constexpr std::string_view name = "Lifo";
    static constexpr std::string_view description = "Owner-local Chase-Lev deque; thieves steal FIFO from the top.";
    static constexpr std::string_view use_case = "Recursive fork-join; owner re-uses hot L1 data across nested "
                                                 "tasks.";
    static constexpr std::size_t typical_submit_ns = 10;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = true;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = false;
    static constexpr bool provides_bounded_latency = false;
};

struct RoundRobin : policy_base {
    static constexpr std::string_view name = "RoundRobin";
    static constexpr std::string_view description = "Per-worker MPSC shards; submits rotate across a counter.";
    static constexpr std::string_view use_case = "Simple, balanced, no global head contention when tasks are "
                                                 "roughly uniform-cost.";
    static constexpr std::size_t typical_submit_ns = 15;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = false;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = false;  // balanced, but not guaranteed
    static constexpr bool provides_bounded_latency = false;
};

struct LocalityAware : policy_base {
    static constexpr std::string_view name = "LocalityAware";
    static constexpr std::string_view description = "Per-L3-shard MPMC; workers drain own L3 first, steal within "
                                                    "NUMA, then cross-NUMA.";
    static constexpr std::string_view use_case = "HPC task dispatch — keeps arena data hot in the consuming "
                                                 "worker's L3.  The DEFAULT for Crucible's workload.";
    static constexpr std::size_t typical_submit_ns = 20;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = true;
    static constexpr bool is_locality_aware = true;
    static constexpr bool provides_fairness = false;
    static constexpr bool provides_bounded_latency = false;
};

struct Deadline : policy_base {
    static constexpr std::string_view name = "Deadline";
    static constexpr std::string_view description = "EDF (Earliest Deadline First); min-heap of jobs by task-"
                                                    "supplied deadline.";
    static constexpr std::string_view use_case = "Real-time workloads with SLA / deadline-miss cost; tasks must "
                                                 "carry a deadline tag.";
    static constexpr std::size_t typical_submit_ns = 50;
    static constexpr bool requires_deadline_tag = true;
    static constexpr bool uses_work_stealing = false;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = false;
    static constexpr bool provides_bounded_latency = true;
};

struct Cfs : policy_base {
    static constexpr std::string_view name = "Cfs";
    static constexpr std::string_view description = "Linux CFS-style; red-black tree of virtual runtimes for "
                                                    "proportional share.";
    static constexpr std::string_view use_case = "Long-lived tasks needing fair-share guarantees over time.";
    static constexpr std::size_t typical_submit_ns = 80;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = false;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = true;
    static constexpr bool provides_bounded_latency = false;
};

struct Eevdf : policy_base {
    static constexpr std::string_view name = "Eevdf";
    static constexpr std::string_view description = "Linux 6.6+ default; earliest eligible virtual deadline with "
                                                    "proportional share and a latency bound.";
    static constexpr std::string_view use_case = "Long-lived tasks needing BOTH fair-share AND bounded "
                                                 "response-latency guarantees.";
    static constexpr std::size_t typical_submit_ns = 100;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = false;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = true;
    static constexpr bool provides_bounded_latency = true;
};

// A named alias so a pool declared without an argument picks one, and
// so a caller can ask for the default without naming it.  Measurement
// can move this to another policy.

using DefaultScheduler = LocalityAware;

template <typename T>
inline constexpr bool is_scheduler_policy_v = std::is_base_of_v<policy_base, T> && !std::is_same_v<T, policy_base>;

template <typename P>
concept SchedulerPolicy = is_scheduler_policy_v<P>;

// The accessors route their rejection through this helper rather than
// constraining themselves directly.  A constrained variable template
// rejects with wording the compiler chooses, and that wording changes
// between releases.  A static_assert here produces a fixed string that
// the negative-compile tests can match on.

namespace detail::sched {

template <typename P, bool IsPolicy>
struct accessor_check;

template <typename P>
struct accessor_check<P, true> {
    static constexpr std::string_view name = P::name;
    static constexpr std::string_view description = P::description;
    static constexpr std::string_view use_case = P::use_case;
    static constexpr std::size_t typical_submit_ns = P::typical_submit_ns;
    static constexpr bool requires_deadline_tag = P::requires_deadline_tag;
    static constexpr bool uses_work_stealing = P::uses_work_stealing;
    static constexpr bool is_locality_aware = P::is_locality_aware;
    static constexpr bool provides_fairness = P::provides_fairness;
    static constexpr bool provides_bounded_latency = P::provides_bounded_latency;
};

template <typename P>
struct accessor_check<P, false> {
    static_assert(is_scheduler_policy_v<P>, "crucible::session::diagnostic [SchedulerAccessor_NonPolicy]: "
                                            "scheduler_name_v / scheduler_description_v / scheduler_use_case_v "
                                            "/ scheduler_submit_ns_v / requires_deadline_tag_v / "
                                            "uses_work_stealing_v / is_locality_aware_v / provides_fairness_v "
                                            "/ provides_bounded_latency_v all require P to be derived from "
                                            "crucible::concurrent::scheduler::policy_base.  See the shipped "
                                            "policies in the Catalog below; a user extension "
                                            "inherits from policy_base and provides the metadata fields.");

    // Arbitrary, and never read: the assertion above is what reaches
    // the user.  They exist so this branch is a complete type.
    static constexpr std::string_view name = "";
    static constexpr std::string_view description = "";
    static constexpr std::string_view use_case = "";
    static constexpr std::size_t typical_submit_ns = 0;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = false;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = false;
    static constexpr bool provides_bounded_latency = false;
};

}  // namespace detail::sched

template <typename P>
inline constexpr std::string_view scheduler_name_v = detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::name;

template <typename P>
inline constexpr std::string_view scheduler_description_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::description;

template <typename P>
inline constexpr std::string_view scheduler_use_case_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::use_case;

template <typename P>
inline constexpr std::size_t scheduler_submit_ns_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::typical_submit_ns;

template <typename P>
inline constexpr bool requires_deadline_tag_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::requires_deadline_tag;

template <typename P>
inline constexpr bool uses_work_stealing_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::uses_work_stealing;

template <typename P>
inline constexpr bool is_locality_aware_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::is_locality_aware;

template <typename P>
inline constexpr bool provides_fairness_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::provides_fairness;

template <typename P>
inline constexpr bool provides_bounded_latency_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::provides_bounded_latency;

// Every shipped policy, so that a bench, a listing or a configuration
// tool can iterate them.

using Catalog = std::tuple<Fifo, Lifo, RoundRobin, LocalityAware, Deadline, Cfs, Eevdf>;

inline constexpr std::size_t catalog_size = std::tuple_size_v<Catalog>;

namespace detail::scheduler_self_test {

static_assert(is_scheduler_policy_v<Fifo>);
static_assert(is_scheduler_policy_v<Lifo>);
static_assert(is_scheduler_policy_v<RoundRobin>);
static_assert(is_scheduler_policy_v<LocalityAware>);
static_assert(is_scheduler_policy_v<Deadline>);
static_assert(is_scheduler_policy_v<Cfs>);
static_assert(is_scheduler_policy_v<Eevdf>);

// The base is the marker, so it is not itself a policy.
static_assert(!is_scheduler_policy_v<policy_base>);

static_assert(!is_scheduler_policy_v<int>);
static_assert(!is_scheduler_policy_v<void>);

struct RandomStruct {};
static_assert(!is_scheduler_policy_v<RandomStruct>);

// Inheritance alone is enough to register an outside policy.
struct CustomLowLatency : policy_base {
    static constexpr std::string_view name = "CustomLowLatency";
    static constexpr std::string_view description = "Single-core busy-spin scheduler for microsecond-budget tasks.";
    static constexpr std::string_view use_case = "HFT-style workloads where wake latency dominates over fairness.";
    static constexpr std::size_t typical_submit_ns = 5;
    static constexpr bool requires_deadline_tag = false;
    static constexpr bool uses_work_stealing = false;
    static constexpr bool is_locality_aware = false;
    static constexpr bool provides_fairness = false;
    static constexpr bool provides_bounded_latency = true;
};
static_assert(is_scheduler_policy_v<CustomLowLatency>);

static_assert(std::is_same_v<DefaultScheduler, LocalityAware>);
static_assert(is_scheduler_policy_v<DefaultScheduler>);

static_assert(scheduler_name_v<Fifo> == "Fifo");
static_assert(scheduler_name_v<Lifo> == "Lifo");
static_assert(scheduler_name_v<RoundRobin> == "RoundRobin");
static_assert(scheduler_name_v<LocalityAware> == "LocalityAware");
static_assert(scheduler_name_v<Deadline> == "Deadline");
static_assert(scheduler_name_v<Cfs> == "Cfs");
static_assert(scheduler_name_v<Eevdf> == "Eevdf");

static_assert(scheduler_name_v<CustomLowLatency> == "CustomLowLatency");

static_assert(!requires_deadline_tag_v<Fifo>);
static_assert(!requires_deadline_tag_v<Lifo>);
static_assert(!requires_deadline_tag_v<RoundRobin>);
static_assert(!requires_deadline_tag_v<LocalityAware>);
static_assert(requires_deadline_tag_v<Deadline>);
static_assert(!requires_deadline_tag_v<Cfs>);
static_assert(!requires_deadline_tag_v<Eevdf>);

static_assert(!uses_work_stealing_v<Fifo>);
static_assert(uses_work_stealing_v<Lifo>);
static_assert(!uses_work_stealing_v<RoundRobin>);
static_assert(uses_work_stealing_v<LocalityAware>);
static_assert(!uses_work_stealing_v<Deadline>);
static_assert(!uses_work_stealing_v<Cfs>);
static_assert(!uses_work_stealing_v<Eevdf>);

static_assert(!is_locality_aware_v<Fifo>);
static_assert(!is_locality_aware_v<Lifo>);
static_assert(!is_locality_aware_v<RoundRobin>);
static_assert(is_locality_aware_v<LocalityAware>);
static_assert(!is_locality_aware_v<Deadline>);
static_assert(!is_locality_aware_v<Cfs>);
static_assert(!is_locality_aware_v<Eevdf>);

static_assert(!provides_fairness_v<Fifo>);
static_assert(!provides_fairness_v<Lifo>);
static_assert(!provides_fairness_v<RoundRobin>);
static_assert(!provides_fairness_v<LocalityAware>);
static_assert(!provides_fairness_v<Deadline>);
static_assert(provides_fairness_v<Cfs>);
static_assert(provides_fairness_v<Eevdf>);

static_assert(!provides_bounded_latency_v<Fifo>);
static_assert(!provides_bounded_latency_v<Lifo>);
static_assert(!provides_bounded_latency_v<RoundRobin>);
static_assert(!provides_bounded_latency_v<LocalityAware>);
static_assert(provides_bounded_latency_v<Deadline>);
static_assert(!provides_bounded_latency_v<Cfs>);
static_assert(provides_bounded_latency_v<Eevdf>);

static_assert(scheduler_submit_ns_v<Fifo> > 0);
static_assert(scheduler_submit_ns_v<Lifo> > 0);
static_assert(scheduler_submit_ns_v<RoundRobin> > 0);
static_assert(scheduler_submit_ns_v<LocalityAware> > 0);
static_assert(scheduler_submit_ns_v<Deadline> > 0);
static_assert(scheduler_submit_ns_v<Cfs> > 0);
static_assert(scheduler_submit_ns_v<Eevdf> > 0);

// The ordering that matters: a richer algorithm costs more per submit.
static_assert(scheduler_submit_ns_v<Deadline> > scheduler_submit_ns_v<Fifo>);
static_assert(scheduler_submit_ns_v<Cfs> > scheduler_submit_ns_v<Fifo>);
static_assert(scheduler_submit_ns_v<Eevdf> > scheduler_submit_ns_v<Cfs>);

template <SchedulerPolicy P>
consteval bool requires_scheduler_policy() {
    return true;
}

static_assert(requires_scheduler_policy<Fifo>());
static_assert(requires_scheduler_policy<LocalityAware>());
static_assert(requires_scheduler_policy<CustomLowLatency>());

static_assert(catalog_size == 7);
static_assert(std::tuple_size_v<Catalog> == 7);

static_assert(is_scheduler_policy_v<std::tuple_element_t<0, Catalog>>);
static_assert(is_scheduler_policy_v<std::tuple_element_t<6, Catalog>>);

// The order is part of the catalog, since callers index into it.
static_assert(std::is_same_v<std::tuple_element_t<0, Catalog>, Fifo>);
static_assert(std::is_same_v<std::tuple_element_t<6, Catalog>, Eevdf>);

template <std::size_t... Is>
consteval bool catalog_names_distinct_impl(std::index_sequence<Is...>) {
    constexpr auto names = std::array<std::string_view, sizeof...(Is)>{std::tuple_element_t<Is, Catalog>::name...};
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) return false;
        }
    }
    return true;
}

consteval bool catalog_names_distinct() {
    return catalog_names_distinct_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_names_distinct());

template <std::size_t... Is>
consteval bool catalog_metadata_non_empty_impl(std::index_sequence<Is...>) {
    return ((!std::tuple_element_t<Is, Catalog>::description.empty()
             && !std::tuple_element_t<Is, Catalog>::use_case.empty())
            && ...);
}

consteval bool catalog_metadata_non_empty() {
    return catalog_metadata_non_empty_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_metadata_non_empty());

// Exactly one shipped policy claims to respect topology, and that is a
// design rule rather than an accident.  A second one either replaces
// the first or needs a finer trait to tell the two apart.

template <std::size_t... Is>
consteval std::size_t count_locality_aware_impl(std::index_sequence<Is...>) {
    return ((std::tuple_element_t<Is, Catalog>::is_locality_aware ? std::size_t{1} : std::size_t{0}) + ...);
}

consteval std::size_t count_locality_aware() {
    return count_locality_aware_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(count_locality_aware() == 1);

}  // namespace detail::scheduler_self_test

}  // namespace crucible::concurrent::scheduler
