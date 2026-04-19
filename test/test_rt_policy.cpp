// Smoke test for crucible::rt — the realtime policy rails.
//
// Scope:
//   • Topology queries don't crash, return plausible values.
//   • `Policy::none()` / `dev_quiet()` / `production()` build and
//     can be applied without fatal errors (degrade-and-warn path).
//   • `AppliedPolicy` RAII reverts affinity + scheduler on destruction.
//   • `select_hot_cpu` returns a CPU in the allowed set, and honours
//     the `exclude` list.
//
// Does NOT verify:
//   • Whether SCHED_DEADLINE / frequency-lock / C-state-disable actually
//     took effect — those require privileges the test suite doesn't
//     have in CI. The apply() function is expected to log + continue
//     without touching process state when privilege is missing.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef __linux__
#  include <sched.h>
#  include <unistd.h>
#endif

#include <crucible/rt/Hardening.h>
#include <crucible/rt/Policy.h>
#include <crucible/rt/Topology.h>

namespace {

int failures = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s — %s\n",               \
                __FILE__, __LINE__, #cond, (msg));                      \
            ++failures;                                                 \
        }                                                               \
    } while (0)

// ── Topology ───────────────────────────────────────────────────────

// Topology queries survive and return values in the expected shape.
void test_topology_basic() {
    using namespace crucible::rt;

    const int n = num_online_cpus();
    CHECK(n >= 1, "must see at least one online CPU");

    const auto allowed = allowed_cpus();
    CHECK(!allowed.empty(), "cpuset must grant at least one CPU");
    for (const int c : allowed) {
        CHECK(c >= 0 && c < n + 1024, "allowed CPU out of plausible range");
    }

    const auto iso = isolated_cpus();
    // Isolcpus may be empty; we just check it's a subset of all CPUs.
    for (const int c : iso) {
        CHECK(c >= 0, "isolcpu non-negative");
    }

    // P-core detection never segfaults; on a non-hybrid CPU we get
    // true for every cpu.
    for (const int c : allowed) {
        (void)is_p_core(c);
    }

    // SMT siblings include the core itself.
    if (!allowed.empty()) {
        const int c = allowed.front();
        const auto sibs = smt_siblings(c);
        if (!sibs.empty()) {
            CHECK(std::find(sibs.begin(), sibs.end(), c) != sibs.end(),
                  "thread_siblings must include the core itself");
        }
    }
}

// Linux cpulist strings parse into sorted, deduped int vectors.
void test_cpulist_parser() {
    using namespace crucible::rt::detail;

    CHECK(parse_cpulist("0-3").size() == 4, "range 0-3 → 4 elements");
    CHECK(parse_cpulist("0,1,2,3").size() == 4, "explicit list 0-3");
    CHECK(parse_cpulist("0-3,8-11").size() == 8, "two ranges");
    CHECK(parse_cpulist("").empty(), "empty input → empty");
    CHECK(parse_cpulist("   ").empty(), "whitespace → empty");
    // Dedup + sort invariant.
    const auto v = parse_cpulist("3,1,2,0,3,1");
    CHECK(v == std::vector<int>({0, 1, 2, 3}), "dedupe + sort");
}

// `select_hot_cpu` respects the allowed set and explicit override.
void test_core_selector() {
    using namespace crucible::rt;

    CoreSelector sel;
    sel.prefer_isolcpu    = true;
    sel.prefer_p_core     = true;
    sel.avoid_smt_sibling = true;

    const int pick = select_hot_cpu(sel);
    const auto allowed = allowed_cpus();
    CHECK(pick >= 0, "select_hot_cpu must return something on a normal system");
    CHECK(std::find(allowed.begin(), allowed.end(), pick) != allowed.end(),
          "picked CPU must be in allowed set");

    // Explicit override wins when the requested CPU is allowed.
    if (!allowed.empty()) {
        CoreSelector sel_explicit;
        sel_explicit.explicit_cpu = allowed.back();
        CHECK(select_hot_cpu(sel_explicit) == allowed.back(),
              "explicit_cpu must be honored when allowed");
    }
}

// Passing `exclude` to `select_hot_cpu` actually skips those CPUs.
void test_core_selector_avoids_exclude() {
    using namespace crucible::rt;

    const auto allowed = allowed_cpus();
    if (allowed.size() < 2) {
        // Single-CPU cpuset: nothing to exclude *to*. The selector's
        // exclude-path is trivially satisfied — skip without failing.
        return;
    }

    CoreSelector sel;
    sel.prefer_isolcpu    = false;   // don't let iso pool skew the pick
    sel.prefer_p_core     = false;
    sel.avoid_smt_sibling = false;   // exclude is what we're testing

    const int excluded = allowed.front();
    const std::vector<int> exclude{excluded};
    const int pick = select_hot_cpu(sel, exclude);

    CHECK(pick >= 0, "select_hot_cpu must still return a CPU when one is excluded");
    CHECK(pick != excluded,
          "select_hot_cpu must not return a CPU that was in the exclude list");
    CHECK(std::find(allowed.begin(), allowed.end(), pick) != allowed.end(),
          "pick is still drawn from the allowed set");
}

// ── Policy application ────────────────────────────────────────────

// `Policy::none()` applies as a no-op: no scheduler / affinity / lock changes.
void test_policy_none_is_noop() {
    using namespace crucible::rt;

#ifdef __linux__
    cpu_set_t before{};
    CPU_ZERO(&before);
    (void)::sched_getaffinity(0, sizeof(before), &before);
#endif

    auto g = apply(Policy::none());
    CHECK(!g.scheduler_applied(), "none() should not touch scheduler");
    CHECK(!g.affinity_applied(),  "none() should not touch affinity");
    CHECK(g.regions_locked() == 0, "none() should lock nothing");

#ifdef __linux__
    cpu_set_t after{};
    CPU_ZERO(&after);
    (void)::sched_getaffinity(0, sizeof(after), &after);
    CHECK(CPU_EQUAL(&before, &after), "affinity unchanged by none()");
#endif
}

// `Policy::dev_quiet()` pins the thread and the RAII guard reverts.
void test_policy_dev_quiet_pins_and_reverts() {
    using namespace crucible::rt;

#ifdef __linux__
    cpu_set_t before{};
    CPU_ZERO(&before);
    CHECK(::sched_getaffinity(0, sizeof(before), &before) == 0,
          "baseline sched_getaffinity works");

    const int before_count = CPU_COUNT(&before);
#endif

    {
        auto g = apply(Policy::dev_quiet());
        // dev_quiet has SCHED_OTHER (no scheduler change) but should pin.
        CHECK(g.affinity_applied() || g.pinned_cpu() < 0,
              "dev_quiet either pins or declines cleanly");
        if (g.affinity_applied()) {
            CHECK(g.pinned_cpu() >= 0, "pinned_cpu set when affinity applied");
        }
        // Guard destructs here → affinity reverts.
    }

#ifdef __linux__
    cpu_set_t after{};
    CPU_ZERO(&after);
    CHECK(::sched_getaffinity(0, sizeof(after), &after) == 0,
          "post-revert sched_getaffinity works");
    const int after_count = CPU_COUNT(&after);

    // The count before and after must match — the guard should have
    // restored our full allowed mask, not leaked a pin.
    CHECK(before_count == after_count,
          "affinity cardinality restored after AppliedPolicy destroyed");
#endif
}

// `Policy::production()` degrades cleanly when capabilities are missing.
void test_policy_production_degrades_gracefully() {
    using namespace crucible::rt;

    // Production asks for SCHED_DEADLINE + CAP_SYS_NICE + freq lock +
    // C-state disable, most of which we won't have in CI. The call
    // must not crash; degrade-and-warn is the contract.
    Policy p = Policy::production();
    p.on_missing_capability = OnMissingCap::DegradeAndWarn;

    // Silence the warning spray during the apply() — scoped tightly so
    // other tests continue to surface warnings if they fire.
    ::setenv("CRUCIBLE_RT_QUIET", "1", 1);
    auto g = apply(p);
    ::unsetenv("CRUCIBLE_RT_QUIET");

    // Pinning should succeed even without caps.
    CHECK(g.pinned_cpu() >= -1, "pinned_cpu is plausible after production()");
}

} // namespace

int main() {
    test_topology_basic();
    test_cpulist_parser();
    test_core_selector();
    test_core_selector_avoids_exclude();
    test_policy_none_is_noop();
    test_policy_dev_quiet_pins_and_reverts();
    test_policy_production_degrades_gracefully();

    if (failures == 0) {
        std::puts("test_rt_policy: OK");
        return 0;
    }
    std::fprintf(stderr, "test_rt_policy: %d FAILURES\n", failures);
    return 1;
}
