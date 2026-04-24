#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::concurrent::scheduler — policy types for thread-pool
//                                    dispatch (SEPLOG-H3, #329)
//
// Type-level vocabulary of dispatch policies for Crucible's
// NumaMpmcThreadPool.  Each policy is a TAG TYPE carrying its name,
// description, algorithmic properties, and typical per-submit cost
// as constexpr metadata.  Selected at compile time; no runtime
// dispatch table, no virtual calls.
//
// ─── Shipped policies ──────────────────────────────────────────────
//
//   Fifo            One shared MPMC queue; strict global FIFO.
//   Lifo            Owner-local Chase-Lev deque; thieves steal FIFO.
//   RoundRobin      Per-worker MPSC shards + rotating submit counter.
//   LocalityAware ★ Per-L3-shard MPMC; workers drain own L3 first,
//                   steal within NUMA, then cross-NUMA.
//   Deadline        EDF min-heap of jobs by task-supplied deadline.
//   Cfs             Linux CFS-style red-black tree of virtual
//                   runtimes; proportional share.
//   Eevdf           Linux 6.6+ default; earliest eligible virtual
//                   deadline + proportional share + latency bound.
//
// ★ LocalityAware is the default — see THREADING.md §5.5.2.  For
// fork-join of short-lived tasks on a contiguous arena (Crucible's
// primary workload), cache locality dominates throughput; Deadline /
// Cfs / Eevdf overhead exceeds the per-task work; Fifo's global head
// is a cache cliff at 16+ workers; RR balances but ignores NUMA.
//
// ─── Metadata per policy ──────────────────────────────────────────
//
// Every policy tag carries:
//
//   ::name                      short identifier
//   ::description               one-sentence algorithmic summary
//   ::use_case                  one-sentence "when to pick this"
//   ::typical_submit_ns         approximate per-submit cost (from
//                               THREADING.md §8.7 bench targets)
//   ::requires_deadline_tag     task must supply a deadline?
//   ::uses_work_stealing        dispatch involves work-stealing?
//   ::is_locality_aware         NUMA / L3 topology respected?
//   ::provides_fairness         fair-share progress guarantee?
//   ::provides_bounded_latency  bounded-latency guarantee (EEVDF)?
//
// Accessors (is_scheduler_policy_v-gated to reject non-policies):
//   scheduler_name_v<P>
//   scheduler_description_v<P>
//   scheduler_use_case_v<P>
//   scheduler_submit_ns_v<P>
//   requires_deadline_tag_v<P>
//   uses_work_stealing_v<P>
//   is_locality_aware_v<P>
//   provides_fairness_v<P>
//   provides_bounded_latency_v<P>
//
// ─── User extension ───────────────────────────────────────────────
//
// New policies inherit from policy_base.  is_scheduler_policy_v
// uses std::is_base_of_v, so user-defined policies plug in with
// zero trait-specialisation boilerplate:
//
//     struct MyCustomScheduler : policy_base {
//         static constexpr std::string_view name = "MyCustom";
//         static constexpr std::string_view description = "...";
//         static constexpr std::string_view use_case    = "...";
//         static constexpr std::size_t typical_submit_ns = 30;
//         static constexpr bool requires_deadline_tag = false;
//         static constexpr bool uses_work_stealing    = true;
//         static constexpr bool is_locality_aware     = true;
//         static constexpr bool provides_fairness     = false;
//         static constexpr bool provides_bounded_latency = false;
//     };
//
//     static_assert(is_scheduler_policy_v<MyCustomScheduler>);
//
// ─── What this ships vs what uses it ──────────────────────────────
//
// This header ships the TYPE VOCABULARY only.  The actual pool that
// consumes these tags (NumaMpmcThreadPool<Policy, Tag>) is the
// subject of SEPLOG-C4 / SEPLOG-H4 (task #314).  Shipping the
// vocabulary now means:
//   * Future pool work can reference the tags without redefining them.
//   * Config / documentation code can annotate scheduler intent today.
//   * Unit tests can be written against tag traits without the pool.
//   * Bench harnesses can parameterise over the full Catalog.
//
// ─── References ───────────────────────────────────────────────────
//
//   THREADING.md §5.5.2 — full rationale for each policy, including
//     when each one is appropriate and why LocalityAware is the
//     default for Crucible's workload.
//   THREADING.md §8.7 — per-policy overhead targets.
//   Goyal-Guo-Weitzman 1996 — Earliest Deadline First (EDF).
//   Ingo Molnar 2007 — CFS in the Linux kernel (2.6.23+).
//   Peter Zijlstra 2023 — EEVDF in Linux 6.6 (replaces CFS default).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::concurrent::scheduler {

// ═════════════════════════════════════════════════════════════════════
// ── policy_base: detection marker ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

struct policy_base {};

// ═════════════════════════════════════════════════════════════════════
// ── The 7 shipped policies ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

struct Fifo : policy_base {
    static constexpr std::string_view name = "Fifo";
    static constexpr std::string_view description =
        "One shared MPMC queue; strict global FIFO dispatch order.";
    static constexpr std::string_view use_case =
        "Ordered processing, simple debugging, strong FIFO invariants.";
    static constexpr std::size_t typical_submit_ns = 20;  // FAA + CAS
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = false;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = false;
    static constexpr bool provides_bounded_latency = false;
};

struct Lifo : policy_base {
    static constexpr std::string_view name = "Lifo";
    static constexpr std::string_view description =
        "Owner-local Chase-Lev deque; thieves steal FIFO from the top.";
    static constexpr std::string_view use_case =
        "Recursive fork-join; owner re-uses hot L1 data across nested "
        "tasks.";
    static constexpr std::size_t typical_submit_ns = 10;  // owner push
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = true;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = false;
    static constexpr bool provides_bounded_latency = false;
};

struct RoundRobin : policy_base {
    static constexpr std::string_view name = "RoundRobin";
    static constexpr std::string_view description =
        "Per-worker MPSC shards; submits rotate across a counter.";
    static constexpr std::string_view use_case =
        "Simple, balanced, no global head contention when tasks are "
        "roughly uniform-cost.";
    static constexpr std::size_t typical_submit_ns = 15;
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = false;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = false;  // not guaranteed
    static constexpr bool provides_bounded_latency = false;
};

struct LocalityAware : policy_base {
    static constexpr std::string_view name = "LocalityAware";
    static constexpr std::string_view description =
        "Per-L3-shard MPMC; workers drain own L3 first, steal within "
        "NUMA, then cross-NUMA.";
    static constexpr std::string_view use_case =
        "HPC task dispatch — keeps arena data hot in the consuming "
        "worker's L3.  The DEFAULT for Crucible's workload.";
    static constexpr std::size_t typical_submit_ns = 20;  // local submit
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = true;
    static constexpr bool is_locality_aware        = true;
    static constexpr bool provides_fairness        = false;
    static constexpr bool provides_bounded_latency = false;
};

struct Deadline : policy_base {
    static constexpr std::string_view name = "Deadline";
    static constexpr std::string_view description =
        "EDF (Earliest Deadline First); min-heap of jobs by task-"
        "supplied deadline.";
    static constexpr std::string_view use_case =
        "Real-time workloads with SLA / deadline-miss cost; tasks must "
        "carry a deadline tag.";
    static constexpr std::size_t typical_submit_ns = 50;  // heap insert
    static constexpr bool requires_deadline_tag    = true;
    static constexpr bool uses_work_stealing       = false;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = false;
    static constexpr bool provides_bounded_latency = true;   // EDF-bounded
};

struct Cfs : policy_base {
    static constexpr std::string_view name = "Cfs";
    static constexpr std::string_view description =
        "Linux CFS-style; red-black tree of virtual runtimes for "
        "proportional share.";
    static constexpr std::string_view use_case =
        "Long-lived tasks needing fair-share guarantees over time.";
    static constexpr std::size_t typical_submit_ns = 80;  // RB-tree insert
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = false;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = true;
    static constexpr bool provides_bounded_latency = false;
};

struct Eevdf : policy_base {
    static constexpr std::string_view name = "Eevdf";
    static constexpr std::string_view description =
        "Linux 6.6+ default; earliest eligible virtual deadline with "
        "proportional share and a latency bound.";
    static constexpr std::string_view use_case =
        "Long-lived tasks needing BOTH fair-share AND bounded "
        "response-latency guarantees.";
    static constexpr std::size_t typical_submit_ns = 100;  // EEVDF ops
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = false;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = true;
    static constexpr bool provides_bounded_latency = true;
};

// ═════════════════════════════════════════════════════════════════════
// ── Default scheduler ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// LocalityAware is Crucible's default per THREADING.md §5.5.2.  Named
// alias here so NumaMpmcThreadPool<> (no template argument) picks the
// right policy, and so code can refer to "the default" without
// committing to a specific name (which could change in a future
// version if measurements show a better default).

using DefaultScheduler = LocalityAware;

// ═════════════════════════════════════════════════════════════════════
// ── Detection + accessor traits ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_scheduler_policy_v =
    std::is_base_of_v<policy_base, T> && !std::is_same_v<T, policy_base>;

template <typename P>
concept SchedulerPolicy = is_scheduler_policy_v<P>;

// ─── Framework-controlled rejection diagnostic ────────────────────
//
// Per task #371, accessor variable templates route their concept
// rejection through a helper struct that fires a STATIC_ASSERT with
// a framework-controlled message — stable across GCC versions.  The
// natural definition (`template <SchedulerPolicy P> inline constexpr
// X = P::field`) emits compiler-version-specific text on rejection
// ("invalid variable template" today; subject to change).  Routing
// through `detail::sched::accessor_check<P, IsPolicy>` gives
// "[SchedulerAccessor_NonPolicy]" prefix that neg-compile tests
// can match against without coupling to GCC's diagnostic phrasing.

namespace detail::sched {

template <typename P, bool IsPolicy>
struct accessor_check;

template <typename P>
struct accessor_check<P, true> {
    static constexpr std::string_view name             = P::name;
    static constexpr std::string_view description      = P::description;
    static constexpr std::string_view use_case         = P::use_case;
    static constexpr std::size_t      typical_submit_ns = P::typical_submit_ns;
    static constexpr bool requires_deadline_tag        = P::requires_deadline_tag;
    static constexpr bool uses_work_stealing           = P::uses_work_stealing;
    static constexpr bool is_locality_aware            = P::is_locality_aware;
    static constexpr bool provides_fairness            = P::provides_fairness;
    static constexpr bool provides_bounded_latency     = P::provides_bounded_latency;
};

template <typename P>
struct accessor_check<P, false> {
    static_assert(is_scheduler_policy_v<P>,
        "crucible::session::diagnostic [SchedulerAccessor_NonPolicy]: "
        "scheduler_name_v / scheduler_description_v / scheduler_use_case_v "
        "/ scheduler_submit_ns_v / requires_deadline_tag_v / "
        "uses_work_stealing_v / is_locality_aware_v / provides_fairness_v "
        "/ provides_bounded_latency_v all require P to be derived from "
        "crucible::concurrent::scheduler::policy_base.  See the shipped "
        "policies in SchedulerPolicy.h's Catalog; user extensions "
        "inherit from policy_base and provide the metadata fields.");

    // Defaults are arbitrary safe values; the static_assert above is
    // what surfaces the violation to the user.
    static constexpr std::string_view name              = "";
    static constexpr std::string_view description       = "";
    static constexpr std::string_view use_case          = "";
    static constexpr std::size_t      typical_submit_ns = 0;
    static constexpr bool requires_deadline_tag         = false;
    static constexpr bool uses_work_stealing            = false;
    static constexpr bool is_locality_aware             = false;
    static constexpr bool provides_fairness             = false;
    static constexpr bool provides_bounded_latency      = false;
};

}  // namespace detail::sched

template <typename P>
inline constexpr std::string_view scheduler_name_v =
    detail::sched::accessor_check<P, is_scheduler_policy_v<P>>::name;

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

// ═════════════════════════════════════════════════════════════════════
// ── Catalog ────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Compile-time tuple of all 7 shipped policies.  Enables iteration
// for catalog-printing, parameterised benches, and config tools.

using Catalog = std::tuple<
    Fifo,
    Lifo,
    RoundRobin,
    LocalityAware,
    Deadline,
    Cfs,
    Eevdf>;

inline constexpr std::size_t catalog_size = std::tuple_size_v<Catalog>;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::scheduler_self_test {

// ─── Detection: all 7 shipped policies recognised ─────────────────

static_assert(is_scheduler_policy_v<Fifo>);
static_assert(is_scheduler_policy_v<Lifo>);
static_assert(is_scheduler_policy_v<RoundRobin>);
static_assert(is_scheduler_policy_v<LocalityAware>);
static_assert(is_scheduler_policy_v<Deadline>);
static_assert(is_scheduler_policy_v<Cfs>);
static_assert(is_scheduler_policy_v<Eevdf>);

// Base class is NOT a policy (it's the marker).
static_assert(!is_scheduler_policy_v<policy_base>);

// Random types are NOT policies.
static_assert(!is_scheduler_policy_v<int>);
static_assert(!is_scheduler_policy_v<void>);

struct RandomStruct {};
static_assert(!is_scheduler_policy_v<RandomStruct>);

// User extension plugs in automatically via inheritance.
struct CustomLowLatency : policy_base {
    static constexpr std::string_view name = "CustomLowLatency";
    static constexpr std::string_view description =
        "Single-core busy-spin scheduler for microsecond-budget tasks.";
    static constexpr std::string_view use_case =
        "HFT-style workloads where wake latency dominates over fairness.";
    static constexpr std::size_t typical_submit_ns = 5;
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = false;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = false;
    static constexpr bool provides_bounded_latency = true;
};
static_assert(is_scheduler_policy_v<CustomLowLatency>);

// ─── DefaultScheduler == LocalityAware ────────────────────────────

static_assert(std::is_same_v<DefaultScheduler, LocalityAware>);
static_assert(is_scheduler_policy_v<DefaultScheduler>);

// ─── Accessors return expected values ─────────────────────────────

static_assert(scheduler_name_v<Fifo>          == "Fifo");
static_assert(scheduler_name_v<Lifo>          == "Lifo");
static_assert(scheduler_name_v<RoundRobin>    == "RoundRobin");
static_assert(scheduler_name_v<LocalityAware> == "LocalityAware");
static_assert(scheduler_name_v<Deadline>      == "Deadline");
static_assert(scheduler_name_v<Cfs>           == "Cfs");
static_assert(scheduler_name_v<Eevdf>         == "Eevdf");

static_assert(scheduler_name_v<CustomLowLatency> == "CustomLowLatency");

// ─── Trait consistency: algorithmic properties match doc claims ───

// Only Deadline requires a deadline tag on tasks.
static_assert(!requires_deadline_tag_v<Fifo>);
static_assert(!requires_deadline_tag_v<Lifo>);
static_assert(!requires_deadline_tag_v<RoundRobin>);
static_assert(!requires_deadline_tag_v<LocalityAware>);
static_assert( requires_deadline_tag_v<Deadline>);
static_assert(!requires_deadline_tag_v<Cfs>);
static_assert(!requires_deadline_tag_v<Eevdf>);

// Lifo and LocalityAware use work-stealing.
static_assert(!uses_work_stealing_v<Fifo>);
static_assert( uses_work_stealing_v<Lifo>);
static_assert(!uses_work_stealing_v<RoundRobin>);
static_assert( uses_work_stealing_v<LocalityAware>);
static_assert(!uses_work_stealing_v<Deadline>);
static_assert(!uses_work_stealing_v<Cfs>);
static_assert(!uses_work_stealing_v<Eevdf>);

// ONLY LocalityAware is locality-aware (by construction — the
// ★-default policy owns this distinction).
static_assert(!is_locality_aware_v<Fifo>);
static_assert(!is_locality_aware_v<Lifo>);
static_assert(!is_locality_aware_v<RoundRobin>);
static_assert( is_locality_aware_v<LocalityAware>);
static_assert(!is_locality_aware_v<Deadline>);
static_assert(!is_locality_aware_v<Cfs>);
static_assert(!is_locality_aware_v<Eevdf>);

// Fairness: only Cfs and Eevdf.
static_assert(!provides_fairness_v<Fifo>);
static_assert(!provides_fairness_v<Lifo>);
static_assert(!provides_fairness_v<RoundRobin>);
static_assert(!provides_fairness_v<LocalityAware>);
static_assert(!provides_fairness_v<Deadline>);
static_assert( provides_fairness_v<Cfs>);
static_assert( provides_fairness_v<Eevdf>);

// Bounded latency: Deadline (via EDF) + Eevdf (by design).
static_assert(!provides_bounded_latency_v<Fifo>);
static_assert(!provides_bounded_latency_v<Lifo>);
static_assert(!provides_bounded_latency_v<RoundRobin>);
static_assert(!provides_bounded_latency_v<LocalityAware>);
static_assert( provides_bounded_latency_v<Deadline>);
static_assert(!provides_bounded_latency_v<Cfs>);
static_assert( provides_bounded_latency_v<Eevdf>);

// ─── Per-policy typical_submit_ns is non-zero and plausible ───────

static_assert(scheduler_submit_ns_v<Fifo>          > 0);
static_assert(scheduler_submit_ns_v<Lifo>          > 0);
static_assert(scheduler_submit_ns_v<RoundRobin>    > 0);
static_assert(scheduler_submit_ns_v<LocalityAware> > 0);
static_assert(scheduler_submit_ns_v<Deadline>      > 0);
static_assert(scheduler_submit_ns_v<Cfs>           > 0);
static_assert(scheduler_submit_ns_v<Eevdf>         > 0);

// Deadline/Cfs/Eevdf overhead > Fifo/RR (more complex algorithms).
static_assert(scheduler_submit_ns_v<Deadline> > scheduler_submit_ns_v<Fifo>);
static_assert(scheduler_submit_ns_v<Cfs>      > scheduler_submit_ns_v<Fifo>);
static_assert(scheduler_submit_ns_v<Eevdf>    > scheduler_submit_ns_v<Cfs>);

// ─── Concept form in requires-clauses ─────────────────────────────

template <SchedulerPolicy P>
consteval bool requires_scheduler_policy() { return true; }

static_assert(requires_scheduler_policy<Fifo>());
static_assert(requires_scheduler_policy<LocalityAware>());
static_assert(requires_scheduler_policy<CustomLowLatency>());

// ─── Catalog ──────────────────────────────────────────────────────

static_assert(catalog_size == 7);
static_assert(std::tuple_size_v<Catalog> == 7);

// All catalog entries are valid scheduler policies.
static_assert(is_scheduler_policy_v<std::tuple_element_t<0, Catalog>>);
static_assert(is_scheduler_policy_v<std::tuple_element_t<6, Catalog>>);

// Deterministic ordering: Catalog[0] = Fifo, Catalog[6] = Eevdf.
static_assert(std::is_same_v<std::tuple_element_t<0, Catalog>, Fifo>);
static_assert(std::is_same_v<std::tuple_element_t<6, Catalog>, Eevdf>);

// ─── Pairwise distinct names ──────────────────────────────────────

template <std::size_t... Is>
consteval bool catalog_names_distinct_impl(std::index_sequence<Is...>) {
    constexpr auto names = std::array<std::string_view, sizeof...(Is)>{
        std::tuple_element_t<Is, Catalog>::name... };
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) return false;
        }
    }
    return true;
}

consteval bool catalog_names_distinct() {
    return catalog_names_distinct_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_names_distinct());

// ─── Descriptions + use cases non-empty ───────────────────────────

template <std::size_t... Is>
consteval bool catalog_metadata_non_empty_impl(std::index_sequence<Is...>) {
    return ((!std::tuple_element_t<Is, Catalog>::description.empty() &&
             !std::tuple_element_t<Is, Catalog>::use_case.empty()) && ...);
}

consteval bool catalog_metadata_non_empty() {
    return catalog_metadata_non_empty_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_metadata_non_empty());

// ─── Exactly ONE policy in the Catalog is locality-aware ──────────
//
// This is a design invariant: LocalityAware is the sole policy that
// respects NUMA/L3 topology.  If a new policy comes along that ALSO
// claims locality awareness, either it should replace LocalityAware
// or the new class needs a sub-trait distinguishing the two.

template <std::size_t... Is>
consteval std::size_t count_locality_aware_impl(std::index_sequence<Is...>) {
    return ((std::tuple_element_t<Is, Catalog>::is_locality_aware
             ? std::size_t{1} : std::size_t{0}) + ...);
}

consteval std::size_t count_locality_aware() {
    return count_locality_aware_impl(
        std::make_index_sequence<catalog_size>{});
}

static_assert(count_locality_aware() == 1);

}  // namespace detail::scheduler_self_test

}  // namespace crucible::concurrent::scheduler
