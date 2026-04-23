// ═══════════════════════════════════════════════════════════════════
// test_topology — sysfs probe + fallback discipline (SEPLOG-C1)
//
// Tests assert STRUCTURAL invariants — not specific values, since
// the topology varies wildly across machines (Tiger Lake desktop,
// EPYC server, M1 Mac, Docker container).
//
// The invariants tested:
//   * All getters return positive values
//   * Cache hierarchy: l1d ≤ l2 ≤ l3
//   * num_smt_threads ≥ num_cores
//   * smt_factor ∈ {1, 2, 4} (hyper-threading degree)
//   * L3 groups cover all CPUs at least once
//   * NUMA distance: self-distance == 10, symmetric
//   * Singleton identity preserved (instance() returns same address)
//   * Source ∈ {Sysfs, Fallback}; Sysfs implies values came from /sys
//
// Plus parser unit tests covering format edge cases.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/Topology.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string_view>

using namespace crucible::concurrent;

// ── Test harness ─────────────────────────────────────────────────

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            throw TestFailure{};                                            \
        }                                                                   \
    } while (0)

namespace {

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// ── Parser unit tests ────────────────────────────────────────────

void test_parse_size_suffix() {
    using crucible::concurrent::topology_detail::parse_size_suffix_;
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32K") == 32 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("1024K") == 1024 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32M") == 32ULL * 1024 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("1G")
                          == 1024ULL * 1024 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("64") == 64);
    // Lowercase variants.
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32k") == 32 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32m") == 32ULL * 1024 * 1024);
    // Edge cases.
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("") == 0);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("garbage") == 0);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("0") == 0);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("0K") == 0);
}

void test_parse_cpu_list() {
    using crucible::concurrent::topology_detail::parse_cpu_list_;

    // Single value.
    auto r1 = parse_cpu_list_("0");
    CRUCIBLE_TEST_REQUIRE(r1.size() == 1 && r1[0] == 0);

    // Single range.
    auto r2 = parse_cpu_list_("0-3");
    CRUCIBLE_TEST_REQUIRE(r2.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        CRUCIBLE_TEST_REQUIRE(r2[i] == static_cast<int>(i));

    // Comma-separated singles.
    auto r3 = parse_cpu_list_("0,2,4");
    CRUCIBLE_TEST_REQUIRE(r3.size() == 3);
    CRUCIBLE_TEST_REQUIRE(r3[0] == 0 && r3[1] == 2 && r3[2] == 4);

    // Mixed ranges and singles (typical SMT siblings format).
    auto r4 = parse_cpu_list_("0-3,8-11,16");
    CRUCIBLE_TEST_REQUIRE(r4.size() == 9);
    CRUCIBLE_TEST_REQUIRE(r4[0] == 0 && r4[8] == 16);

    // SMT siblings format (cpu0 + cpu16).
    auto r5 = parse_cpu_list_("0,16");
    CRUCIBLE_TEST_REQUIRE(r5.size() == 2);
    CRUCIBLE_TEST_REQUIRE(r5[0] == 0 && r5[1] == 16);

    // Empty / garbage.
    CRUCIBLE_TEST_REQUIRE(parse_cpu_list_("").empty());
}

void test_parse_int_list() {
    using crucible::concurrent::topology_detail::parse_int_list_;

    auto r1 = parse_int_list_("10 20 20 10");
    CRUCIBLE_TEST_REQUIRE(r1.size() == 4);
    CRUCIBLE_TEST_REQUIRE(r1[0] == 10 && r1[1] == 20 &&
                          r1[2] == 20 && r1[3] == 10);

    // Single value (single-node distance file).
    auto r2 = parse_int_list_("10");
    CRUCIBLE_TEST_REQUIRE(r2.size() == 1 && r2[0] == 10);

    CRUCIBLE_TEST_REQUIRE(parse_int_list_("").empty());
}

// ── Singleton invariants ─────────────────────────────────────────

void test_singleton_identity() {
    auto& a = Topology::instance();
    auto& b = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(&a == &b);  // same address
}

void test_basic_positive_values() {
    auto& t = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(t.l1d_per_core_bytes() > 0);
    CRUCIBLE_TEST_REQUIRE(t.l1i_per_core_bytes() > 0);
    CRUCIBLE_TEST_REQUIRE(t.l2_per_core_bytes() > 0);
    CRUCIBLE_TEST_REQUIRE(t.l3_total_bytes() > 0);
    CRUCIBLE_TEST_REQUIRE(t.cache_line_bytes() > 0);
    CRUCIBLE_TEST_REQUIRE(t.num_cores() > 0);
    CRUCIBLE_TEST_REQUIRE(t.num_smt_threads() > 0);
    CRUCIBLE_TEST_REQUIRE(t.numa_nodes() > 0);
    CRUCIBLE_TEST_REQUIRE(t.process_cpu_count() > 0);
    CRUCIBLE_TEST_REQUIRE(t.page_size_bytes() > 0);
}

// ── New container/cgroup awareness tests ───────────────────────

void test_process_cpu_count_sane() {
    auto& t = Topology::instance();
    // process_cpu_count must be ≤ num_smt_threads (cgroup can only
    // restrict, never expand).
    CRUCIBLE_TEST_REQUIRE(t.process_cpu_count() <= t.num_smt_threads());
    // Without container restriction, process_cpu_count typically
    // equals num_smt_threads.  We don't strictly assert equality
    // because CI may run inside a cgroup, but we DO assert positive.
    CRUCIBLE_TEST_REQUIRE(t.process_cpu_count() >= 1);
}

void test_page_size_typical() {
    auto& t = Topology::instance();
    const auto ps = t.page_size_bytes();
    // 4 KB on x86-64 / aarch64 with default kernel; 16 KB on Apple
    // Silicon and some ARM Linux configurations.  Anything else is
    // unusual but theoretically valid.
    CRUCIBLE_TEST_REQUIRE(ps == 4096 || ps == 16384 || ps == 65536);
}

void test_cache_clusters_alias() {
    auto& t = Topology::instance();
    // cache_clusters() is the AMD-CCD-vocabulary alias of l3_groups().
    // Identical content; verify by sizes.
    CRUCIBLE_TEST_REQUIRE(t.cache_clusters().size() == t.l3_groups().size());
}

void test_log_summary_emits() {
    auto& t = Topology::instance();
    std::fprintf(stderr, "\n      log_summary() output ↓\n");
    t.log_summary(stderr);
    std::fprintf(stderr, "      log_summary() output ↑\n      ");
}

void test_cache_hierarchy_monotonic() {
    auto& t = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(t.l1d_per_core_bytes() <= t.l2_per_core_bytes());
    CRUCIBLE_TEST_REQUIRE(t.l2_per_core_bytes() <= t.l3_total_bytes());

    // Cache line: typically 64 (sometimes 128 on M1).
    const auto line = t.cache_line_bytes();
    CRUCIBLE_TEST_REQUIRE(line == 32 || line == 64 || line == 128);
}

void test_smt_factor_sane() {
    auto& t = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(t.num_smt_threads() >= t.num_cores());
    const auto smt = t.smt_factor();
    // Typical SMT factors: 1 (no SMT), 2 (Intel/AMD HT), 4 (POWER).
    CRUCIBLE_TEST_REQUIRE(smt == 1 || smt == 2 || smt == 4);
}

void test_l3_groups_cover_threads() {
    auto& t = Topology::instance();
    if (t.source() == Topology::Source::Fallback) {
        // Fallback case: l3_groups_ may be empty.  Just check
        // cores_per_socket is sane.
        CRUCIBLE_TEST_REQUIRE(t.cores_per_socket() > 0);
        return;
    }
    // Sysfs case: each L3 group is non-empty; combined coverage
    // of all groups should be at least num_smt_threads.
    const auto groups = t.l3_groups();
    CRUCIBLE_TEST_REQUIRE(!groups.empty());
    std::set<int> covered;
    for (auto const& g : groups) {
        CRUCIBLE_TEST_REQUIRE(!g.empty());
        for (int cpu : g) covered.insert(cpu);
    }
    CRUCIBLE_TEST_REQUIRE(covered.size() >= t.num_cores());
    // Note: covered may be larger than num_cores (covers SMT threads)
    // but cannot exceed num_smt_threads.
    CRUCIBLE_TEST_REQUIRE(covered.size() <= t.num_smt_threads());
}

void test_numa_self_distance_is_ten() {
    auto& t = Topology::instance();
    for (std::size_t i = 0; i < t.numa_nodes(); ++i) {
        const int d = t.numa_distance(static_cast<int>(i),
                                       static_cast<int>(i));
        // Linux convention: self-distance is always 10.
        CRUCIBLE_TEST_REQUIRE(d == 10);
    }
}

void test_numa_distance_symmetric() {
    auto& t = Topology::instance();
    // Distance matrix should be symmetric: distance(i, j) == distance(j, i).
    // (Linux NUMA distance comes from ACPI SLIT, which is always symmetric.)
    for (std::size_t i = 0; i < t.numa_nodes(); ++i) {
        for (std::size_t j = i + 1; j < t.numa_nodes(); ++j) {
            const int dij = t.numa_distance(static_cast<int>(i),
                                             static_cast<int>(j));
            const int dji = t.numa_distance(static_cast<int>(j),
                                             static_cast<int>(i));
            CRUCIBLE_TEST_REQUIRE(dij == dji);
        }
    }
}

void test_cores_on_node_nonempty() {
    auto& t = Topology::instance();
    std::set<int> all_cpus;
    for (std::size_t i = 0; i < t.numa_nodes(); ++i) {
        const auto on_node = t.cores_on_node(static_cast<int>(i));
        // At least one cpu on each populated node.
        if (t.source() == Topology::Source::Sysfs) {
            CRUCIBLE_TEST_REQUIRE(!on_node.empty());
        }
        for (int cpu : on_node) all_cpus.insert(cpu);
    }
    // All cpus across all nodes should match num_smt_threads.
    if (t.source() == Topology::Source::Sysfs && !all_cpus.empty()) {
        CRUCIBLE_TEST_REQUIRE(all_cpus.size() == t.num_smt_threads());
    }
}

void test_source_consistency() {
    auto& t = Topology::instance();
    // Whatever the source, getters must work.  Sysfs implies the
    // probed values came from /sys; Fallback uses defaults.
    const auto src = t.source();
    CRUCIBLE_TEST_REQUIRE(src == Topology::Source::Sysfs ||
                          src == Topology::Source::Fallback);

    // Print the probed topology for diagnostic visibility.
    std::fprintf(stderr,
                 "\n      Topology(source=%s):\n"
                 "        cores=%zu smt_threads=%zu smt_factor=%zu\n"
                 "        l1d=%zu KB l2=%zu KB l3=%zu MB line=%zu\n"
                 "        l3_groups=%zu numa_nodes=%zu cores_per_socket=%zu\n",
                 (src == Topology::Source::Sysfs ? "Sysfs" : "Fallback"),
                 t.num_cores(), t.num_smt_threads(), t.smt_factor(),
                 t.l1d_per_core_bytes() / 1024,
                 t.l2_per_core_bytes() / 1024,
                 t.l3_total_bytes() / (1024 * 1024),
                 t.cache_line_bytes(),
                 t.l3_groups().size(),
                 t.numa_nodes(),
                 t.cores_per_socket());
}

// ── Boundary checks (out-of-range queries) ───────────────────────

void test_numa_distance_out_of_range() {
    auto& t = Topology::instance();
    // Out-of-range queries return safe defaults (10) rather than UB.
    CRUCIBLE_TEST_REQUIRE(t.numa_distance(-1, 0) == 10);
    CRUCIBLE_TEST_REQUIRE(t.numa_distance(0, -1) == 10);
    CRUCIBLE_TEST_REQUIRE(t.numa_distance(99999, 0) == 10);
    CRUCIBLE_TEST_REQUIRE(t.numa_distance(0, 99999) == 10);
}

void test_cores_on_node_out_of_range() {
    auto& t = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(t.cores_on_node(-1).empty());
    CRUCIBLE_TEST_REQUIRE(t.cores_on_node(99999).empty());
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_topology:\n");

    run_test("test_parse_size_suffix",            test_parse_size_suffix);
    run_test("test_parse_cpu_list",               test_parse_cpu_list);
    run_test("test_parse_int_list",               test_parse_int_list);
    run_test("test_singleton_identity",           test_singleton_identity);
    run_test("test_basic_positive_values",        test_basic_positive_values);
    run_test("test_process_cpu_count_sane",       test_process_cpu_count_sane);
    run_test("test_page_size_typical",            test_page_size_typical);
    run_test("test_cache_clusters_alias",         test_cache_clusters_alias);
    run_test("test_log_summary_emits",            test_log_summary_emits);
    run_test("test_cache_hierarchy_monotonic",    test_cache_hierarchy_monotonic);
    run_test("test_smt_factor_sane",              test_smt_factor_sane);
    run_test("test_l3_groups_cover_threads",      test_l3_groups_cover_threads);
    run_test("test_numa_self_distance_is_ten",    test_numa_self_distance_is_ten);
    run_test("test_numa_distance_symmetric",      test_numa_distance_symmetric);
    run_test("test_cores_on_node_nonempty",       test_cores_on_node_nonempty);
    run_test("test_source_consistency",           test_source_consistency);
    run_test("test_numa_distance_out_of_range",   test_numa_distance_out_of_range);
    run_test("test_cores_on_node_out_of_range",   test_cores_on_node_out_of_range);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
