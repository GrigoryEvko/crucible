// The suite runs unprivileged, so it cannot verify that SCHED_DEADLINE,
// the frequency lock or the C-state disable took effect.  What it can
// check is that apply() warns and continues rather than failing when a
// privilege is missing.  That is why several assertions below accept
// either outcome.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#endif

#include <crucible/warden/Hardening.h>
#include <crucible/warden/Policy.h>
#include <crucible/warden/Registry.h>
#include <crucible/warden/CpuTopology.h>

namespace {

int failures = 0;

#define CHECK(cond, msg)                                                                     \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d  %s — %s\n", __FILE__, __LINE__, #cond, (msg)); \
            ++failures;                                                                      \
        }                                                                                    \
    } while (0)

void test_topology_basic() {
    using namespace crucible::warden;

    const int n = num_online_cpus();
    CHECK(n >= 1, "must see at least one online CPU");

    const auto allowed = allowed_cpus();
    CHECK(!allowed.empty(), "cpuset must grant at least one CPU");
    for (const int c : allowed) {
        CHECK(c >= 0 && c < n + 1024, "allowed CPU out of plausible range");
    }

    const auto iso = isolated_cpus();
    // The isolated set is legitimately empty on most systems, so only
    // the shape of an entry can be asserted.
    for (const int c : iso) {
        CHECK(c >= 0, "isolcpu non-negative");
    }

    // The result is discarded: the claim is only that probing a
    // non-hybrid CPU is safe.
    for (const int c : allowed) {
        (void)is_p_core(c);
    }

    if (!allowed.empty()) {
        const int c = allowed.front();
        const auto sibs = smt_siblings(c);
        if (!sibs.empty()) {
            CHECK(std::find(sibs.begin(), sibs.end(), c) != sibs.end(), "thread_siblings must include the core itself");
        }
    }
}

// The input strings are the shapes the kernel writes into the sysfs and
// procfs cpulist files: a range, an explicit list, several ranges, and
// an empty file.
void test_cpulist_parser() {
    using namespace crucible::warden::detail;

    CHECK(parse_cpulist("0-3").size() == 4, "range 0-3 → 4 elements");
    CHECK(parse_cpulist("0,1,2,3").size() == 4, "explicit list 0-3");
    CHECK(parse_cpulist("0-3,8-11").size() == 8, "two ranges");
    CHECK(parse_cpulist("").empty(), "empty input → empty");
    CHECK(parse_cpulist("   ").empty(), "whitespace → empty");
    const auto v = parse_cpulist("3,1,2,0,3,1");
    CHECK(v == std::vector<int>({0, 1, 2, 3}), "dedupe + sort");
}

void test_core_selector() {
    using namespace crucible::warden;

    CoreSelector sel;
    sel.prefer_isolcpu = true;
    sel.prefer_p_core = true;
    sel.avoid_smt_sibling = true;

    const int pick = select_hot_cpu(sel);
    const auto allowed = allowed_cpus();
    CHECK(pick >= 0, "select_hot_cpu must return something on a normal system");
    CHECK(std::find(allowed.begin(), allowed.end(), pick) != allowed.end(), "picked CPU must be in allowed set");

    if (!allowed.empty()) {
        CoreSelector sel_explicit;
        sel_explicit.explicit_cpu = allowed.back();
        CHECK(select_hot_cpu(sel_explicit) == allowed.back(), "explicit_cpu must be honored when allowed");
    }
}

// cpu 0 absorbs timer interrupts and RCU callbacks on most Linux
// configurations, so its cache state is the least predictable member of
// the allowed set.  That is the reason the default selector steers away
// from it.
void test_core_selector_avoids_cpu0() {
    using namespace crucible::warden;

    const auto allowed = allowed_cpus();
    // The heuristic is only exercisable when cpu 0 is in the cpuset and
    // at least one other CPU exists to steer towards.  A runner inside a
    // cpu-constrained cgroup legitimately takes the skip.
    const bool have0 = std::find(allowed.begin(), allowed.end(), 0) != allowed.end();
    if (!have0 || allowed.size() < 2) return;

    CoreSelector sel;  // default: avoid_cpu0 = true
    const int pick = select_hot_cpu(sel);
    CHECK(pick >= 0, "select_hot_cpu returns something");
    CHECK(pick != 0, "default selector avoids cpu0 when another CPU is available");

    // With every scoring signal flattened the lowest-index tie-break is
    // all that remains, so opting out of the cpu0 rule must return cpu0.
    CoreSelector sel_legacy;
    sel_legacy.avoid_cpu0 = false;
    sel_legacy.prefer_p_core = false;  // flatten any P/E signal
    sel_legacy.prefer_isolcpu = false;  // ignore isolcpus
    const int pick_legacy = select_hot_cpu(sel_legacy);
    CHECK(pick_legacy == 0, "avoid_cpu0=false with flat scoring returns cpu0");
}

void test_core_selector_avoids_exclude() {
    using namespace crucible::warden;

    const auto allowed = allowed_cpus();
    if (allowed.size() < 2) {
        // A single-CPU cpuset leaves nowhere to steer to, which
        // satisfies the exclude path trivially.
        return;
    }

    CoreSelector sel;
    sel.prefer_isolcpu = false;  // don't let iso pool skew the pick
    sel.prefer_p_core = false;
    sel.avoid_smt_sibling = false;  // exclude is what we're testing

    const int excluded = allowed.front();
    const std::vector<int> exclude{excluded};
    const int pick = select_hot_cpu(sel, exclude);

    CHECK(pick >= 0, "select_hot_cpu must still return a CPU when one is excluded");
    CHECK(pick != excluded, "select_hot_cpu must not return a CPU that was in the exclude list");
    CHECK(std::find(allowed.begin(), allowed.end(), pick) != allowed.end(), "pick is still drawn from the allowed set");
}

void test_policy_none_is_noop() {
    using namespace crucible::warden;

#ifdef __linux__
    cpu_set_t before{};
    CPU_ZERO(&before);
    (void)::sched_getaffinity(0, sizeof(before), &before);
#endif

    auto g = apply(Policy::none());
    CHECK(!g.scheduler_applied(), "none() should not touch scheduler");
    CHECK(!g.affinity_applied(), "none() should not touch affinity");
    CHECK(g.regions_locked() == 0, "none() should lock nothing");

#ifdef __linux__
    cpu_set_t after{};
    CPU_ZERO(&after);
    (void)::sched_getaffinity(0, sizeof(after), &after);
    CHECK(CPU_EQUAL(&before, &after), "affinity unchanged by none()");
#endif
}

void test_policy_dev_quiet_pins_and_reverts() {
    using namespace crucible::warden;

#ifdef __linux__
    cpu_set_t before{};
    CPU_ZERO(&before);
    CHECK(::sched_getaffinity(0, sizeof(before), &before) == 0, "baseline sched_getaffinity works");

    const int before_count = CPU_COUNT(&before);
#endif

    {
        auto g = apply(Policy::dev_quiet());
        // This policy stays on SCHED_OTHER, so pinning is the only
        // change it can make, and declining to pin is also legal.
        CHECK(g.affinity_applied() || g.pinned_cpu() < 0, "dev_quiet either pins or declines cleanly");
        if (g.affinity_applied()) {
            CHECK(g.pinned_cpu() >= 0, "pinned_cpu set when affinity applied");
        }
    }

#ifdef __linux__
    cpu_set_t after{};
    CPU_ZERO(&after);
    CHECK(::sched_getaffinity(0, sizeof(after), &after) == 0, "post-revert sched_getaffinity works");
    const int after_count = CPU_COUNT(&after);

    // A leaked pin would narrow the mask, so cardinality is enough to
    // catch it without assuming which CPU was picked.
    CHECK(before_count == after_count, "affinity cardinality restored after AppliedPolicy destroyed");
#endif
}

// revert() must leave the handle fully disarmed: not merely flagged as
// reverted, but with every observer reporting the post-revert truth.
// Move assignment reverts the target and then swaps state in, so a
// stale observer flag rides into the moved-from source and reports it
// as still active, even though its destructor correctly does nothing.
void test_revert_clears_observers() {
    using namespace crucible::warden;

#ifdef __linux__
    ::setenv("CRUCIBLE_WARDEN_QUIET", "1", 1);

    // Explicit early revert.
    {
        auto g = apply(Policy::dev_quiet());
        const bool had_affinity = g.affinity_applied();
        const int had_pin = g.pinned_cpu();
        (void)had_pin;
        g.revert();
        CHECK(!g.scheduler_applied(), "scheduler_applied() must be false after revert");
        CHECK(!g.affinity_applied(), "affinity_applied() must be false after revert");
        CHECK(g.regions_locked() == 0, "regions_locked() must be zero after revert");
        CHECK(g.pinned_cpu() == -1, "pinned_cpu() must be -1 after revert");
        (void)had_affinity;
    }

    // Move assignment.
    {
        auto lhs = apply(Policy::dev_quiet());
        auto rhs = apply(Policy::dev_quiet());
        lhs = std::move(rhs);
        // NOLINTNEXTLINE(bugprone-use-after-move) — reading the
        // moved-from object is the point: a disarmed one must report
        // not-applied for every observer.
        CHECK(!rhs.scheduler_applied(), "moved-from rhs must NOT report scheduler_applied");
        CHECK(!rhs.affinity_applied(), "moved-from rhs must NOT report affinity_applied");
        CHECK(rhs.regions_locked() == 0, "moved-from rhs must report zero locked regions");
        CHECK(rhs.pinned_cpu() == -1, "moved-from rhs must report pinned_cpu == -1");
    }

    ::unsetenv("CRUCIBLE_WARDEN_QUIET");
#endif
}

void test_policy_production_degrades_gracefully() {
    using namespace crucible::warden;

    // This policy asks for SCHED_DEADLINE, CAP_SYS_NICE, a frequency
    // lock and a C-state disable, most of which are absent here.  The
    // claim is only that apply() returns instead of failing.
    Policy p = Policy::production();
    p.on_missing_capability = OnMissingCap::DegradeAndWarn;

    // The quiet flag is scoped tightly around apply() so that later
    // tests still surface a warning if one fires.
    ::setenv("CRUCIBLE_WARDEN_QUIET", "1", 1);
    auto g = apply(p);
    ::unsetenv("CRUCIBLE_WARDEN_QUIET");

    CHECK(g.pinned_cpu() >= -1, "pinned_cpu is plausible after production()");
}

void test_registry_basic() {
    using namespace crucible::warden;

    auto& reg = HotRegionRegistry::instance();
    const size_t baseline = reg.size();

    alignas(64) unsigned char buf[4096]{};
    register_hot_region(buf, sizeof(buf), /*huge=*/false, "test_registry_basic");
    CHECK(reg.size() == baseline + 1, "one registration bumps size");

    register_hot_region(buf, sizeof(buf), /*huge=*/true, "test_registry_basic_updated");
    CHECK(reg.size() == baseline + 1, "re-register same addr is an update, not a new slot");

    auto snap = reg.snapshot();
    bool found = false;
    for (const auto& r : snap) {
        if (r.addr == buf) {
            CHECK(r.len == sizeof(buf), "len preserved on update");
            CHECK(r.huge_hint, "huge_hint updated to true");
            found = true;
            break;
        }
    }
    CHECK(found, "registered region appears in snapshot");

    unregister_hot_region(buf);
    CHECK(reg.size() == baseline, "unregister restores size");

    unregister_hot_region(buf);
    CHECK(reg.size() == baseline, "double-unregister is a no-op");
}

// Whether the mlock took effect is unobservable without CAP_IPC_LOCK,
// so the claim is narrower: apply() walks the registry and teardown
// leaves the registry at the size it started from.
void test_registry_applies_on_apply() {
    using namespace crucible::warden;

    alignas(64) unsigned char buf[4096]{};
    const size_t baseline = HotRegionRegistry::instance().size();

    register_hot_region(buf, sizeof(buf), /*huge=*/false, "test_registry_applies");
    CHECK(HotRegionRegistry::instance().size() == baseline + 1, "buf registered");

    // This policy enables the region lock while staying on SCHED_OTHER,
    // so the walk runs without needing CAP_SYS_NICE.
    ::setenv("CRUCIBLE_WARDEN_QUIET", "1", 1);
    {
        auto g = apply(Policy::dev_quiet());
        // The count is nonzero only where CAP_IPC_LOCK or memlock
        // headroom exists, so it carries no claim here.
        (void)g.regions_locked();
    }
    ::unsetenv("CRUCIBLE_WARDEN_QUIET");

    unregister_hot_region(buf);
    CHECK(HotRegionRegistry::instance().size() == baseline, "registry clean after unregister");
}

}  // namespace

int main() {
    test_topology_basic();
    test_cpulist_parser();
    test_core_selector();
    test_core_selector_avoids_cpu0();
    test_core_selector_avoids_exclude();
    test_policy_none_is_noop();
    test_policy_dev_quiet_pins_and_reverts();
    test_revert_clears_observers();
    test_policy_production_degrades_gracefully();
    test_registry_basic();
    test_registry_applies_on_apply();

    if (failures == 0) {
        std::puts("test_warden_policy: OK");
        return 0;
    }
    std::fprintf(stderr, "test_warden_policy: %d FAILURES\n", failures);
    return 1;
}
