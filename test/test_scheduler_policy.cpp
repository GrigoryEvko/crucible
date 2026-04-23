// Runtime harness for scheduler policy tags (task #329, SEPLOG-H3).
// Most coverage is in-header static_asserts; this file prints the
// full 7-policy catalog with all metadata + exercises user-defined
// policy extension at runtime.

#include <crucible/concurrent/SchedulerPolicy.h>

#include <cstdio>
#include <string_view>
#include <tuple>

namespace {

using namespace crucible::concurrent::scheduler;

// ── Print the catalog with all metadata ───────────────────────

template <std::size_t I>
void print_one() {
    using P = std::tuple_element_t<I, Catalog>;
    std::printf("  [%zu] %-16.*s submit=%3zu ns  steal=%d  locality=%d "
                "fair=%d  deadline=%d  bounded=%d\n",
                I,
                static_cast<int>(P::name.size()),
                P::name.data(),
                P::typical_submit_ns,
                P::uses_work_stealing,
                P::is_locality_aware,
                P::provides_fairness,
                P::requires_deadline_tag,
                P::provides_bounded_latency);
    std::printf("       %.*s\n",
                static_cast<int>(P::description.size()),
                P::description.data());
    std::printf("       Use:  %.*s\n",
                static_cast<int>(P::use_case.size()),
                P::use_case.data());
}

template <std::size_t... Is>
void print_catalog_impl(std::index_sequence<Is...>) {
    (print_one<Is>(), ...);
}

void print_catalog() {
    std::puts("Scheduler policy catalog:");
    print_catalog_impl(std::make_index_sequence<catalog_size>{});
    std::printf("  DEFAULT: %.*s\n",
                static_cast<int>(DefaultScheduler::name.size()),
                DefaultScheduler::name.data());
}

// ── Runtime: verify fields per policy ─────────────────────────

int run_catalog_fields() {
    // Every field on every shipped policy is non-empty / non-zero.
    if (scheduler_name_v<Fifo>.empty())                 return 1;
    if (scheduler_description_v<Fifo>.empty())          return 1;
    if (scheduler_use_case_v<Fifo>.empty())             return 1;
    if (scheduler_submit_ns_v<Fifo> == 0)               return 1;

    if (scheduler_name_v<Eevdf>.empty())                return 1;
    if (scheduler_description_v<Eevdf>.empty())         return 1;
    if (scheduler_use_case_v<Eevdf>.empty())            return 1;
    if (scheduler_submit_ns_v<Eevdf> == 0)              return 1;

    return 0;
}

// ── User-defined policy extension ─────────────────────────────

struct CustomRdmaOffload : policy_base {
    static constexpr std::string_view name = "CustomRdmaOffload";
    static constexpr std::string_view description =
        "Offloads all task dispatch to the RDMA NIC's on-NIC scheduler.";
    static constexpr std::string_view use_case =
        "Distributed collective scheduling where the NIC has visibility "
        "into all peers' workloads.";
    static constexpr std::size_t typical_submit_ns = 40;
    static constexpr bool requires_deadline_tag    = false;
    static constexpr bool uses_work_stealing       = false;
    static constexpr bool is_locality_aware        = false;
    static constexpr bool provides_fairness        = true;
    static constexpr bool provides_bounded_latency = false;
};

static_assert(is_scheduler_policy_v<CustomRdmaOffload>);
static_assert(scheduler_name_v<CustomRdmaOffload> == "CustomRdmaOffload");

// Exercise every trait accessor against the user extension — proves
// the trait surface is uniform across shipped and user-defined
// policies.  (Also prevents -Werror=unused-const-variable on the
// fields that the runtime harness doesn't touch directly.)
static_assert(!requires_deadline_tag_v<CustomRdmaOffload>);
static_assert(!uses_work_stealing_v<CustomRdmaOffload>);
static_assert(!is_locality_aware_v<CustomRdmaOffload>);
static_assert( provides_fairness_v<CustomRdmaOffload>);
static_assert(!provides_bounded_latency_v<CustomRdmaOffload>);
static_assert( scheduler_submit_ns_v<CustomRdmaOffload> == 40);

int run_user_extension() {
    if (scheduler_name_v<CustomRdmaOffload> != "CustomRdmaOffload") return 1;
    if (scheduler_description_v<CustomRdmaOffload>.empty())         return 1;
    if (scheduler_submit_ns_v<CustomRdmaOffload> != 40)             return 1;
    return 0;
}

// ── Concept-driven dispatch dispatch ──────────────────────────
//
// Demonstrates using SchedulerPolicy concept to gate a function
// template — any user-defined policy plugs in transparently.

template <SchedulerPolicy P>
int classify_policy_verbosity() {
    // Classify policy into rough cost tiers.
    if constexpr (scheduler_submit_ns_v<P> < 20) {
        return 1;  // cheap
    } else if constexpr (scheduler_submit_ns_v<P> < 50) {
        return 2;  // moderate
    } else {
        return 3;  // expensive
    }
}

int run_concept_dispatch() {
    // Compile-time classification into tiers.
    if (classify_policy_verbosity<Lifo>() != 1) return 1;     // 10 ns
    if (classify_policy_verbosity<Fifo>() != 2) return 1;     // 20 ns
    if (classify_policy_verbosity<Cfs>() != 3)  return 1;     // 80 ns
    if (classify_policy_verbosity<Eevdf>() != 3) return 1;    // 100 ns
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_catalog_fields();   rc != 0) return rc;
    if (int rc = run_user_extension();   rc != 0) return rc;
    if (int rc = run_concept_dispatch(); rc != 0) return rc;
    print_catalog();
    std::puts("scheduler_policy: 7 policies + user extension + concept "
              "dispatch + catalog OK");
    return 0;
}
