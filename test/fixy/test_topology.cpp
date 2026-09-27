// The machine under test is unknown: a laptop, a many-socket server, a
// container with two cpus.  So every assertion here is structural.  It
// bounds a value, relates two values, or names the small set a value
// may come from, and never pins the number itself.

#include <fixy/concurrent/Topology.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

using namespace fixy::concurrent;

namespace eff = foundation::effects;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
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

// These are the spellings the kernel writes into the cache-size files,
// including the lowercase forms and the empty file.
void test_parse_size_suffix() {
    using fixy::concurrent::topology_detail::parse_size_suffix_;
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32K") == 32 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("1024K") == 1024 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32M") == 32ULL * 1024 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("1G") == 1024ULL * 1024 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("64") == 64);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32k") == 32 * 1024);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("32m") == 32ULL * 1024 * 1024);
    // Unreadable input yields zero, which every caller treats as
    // absent rather than as a size.
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("") == 0);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("garbage") == 0);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("0") == 0);
    CRUCIBLE_TEST_REQUIRE(parse_size_suffix_("0K") == 0);
}

void test_parse_cpu_list() {
    using fixy::concurrent::topology_detail::parse_cpu_list_;

    auto r1 = parse_cpu_list_("0");
    CRUCIBLE_TEST_REQUIRE(r1.size() == 1 && r1[0] == 0);

    auto r2 = parse_cpu_list_("0-3");
    CRUCIBLE_TEST_REQUIRE(r2.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        CRUCIBLE_TEST_REQUIRE(r2[i] == static_cast<int>(i));

    auto r3 = parse_cpu_list_("0,2,4");
    CRUCIBLE_TEST_REQUIRE(r3.size() == 3);
    CRUCIBLE_TEST_REQUIRE(r3[0] == 0 && r3[1] == 2 && r3[2] == 4);

    // Ranges and singles mix in one file, which is how a topology file
    // spells a set that is not contiguous.
    auto r4 = parse_cpu_list_("0-3,8-11,16");
    CRUCIBLE_TEST_REQUIRE(r4.size() == 9);
    CRUCIBLE_TEST_REQUIRE(r4[0] == 0 && r4[8] == 16);

    // A pair of far-apart numbers is what a sibling list looks like on
    // a machine with symmetric multithreading.
    auto r5 = parse_cpu_list_("0,16");
    CRUCIBLE_TEST_REQUIRE(r5.size() == 2);
    CRUCIBLE_TEST_REQUIRE(r5[0] == 0 && r5[1] == 16);

    CRUCIBLE_TEST_REQUIRE(parse_cpu_list_("").empty());
}

void test_parse_int_list() {
    using fixy::concurrent::topology_detail::parse_int_list_;

    auto r1 = parse_int_list_("10 20 20 10");
    CRUCIBLE_TEST_REQUIRE(r1.size() == 4);
    CRUCIBLE_TEST_REQUIRE(r1[0] == 10 && r1[1] == 20 && r1[2] == 20 && r1[3] == 10);

    // A machine with one node writes a distance file holding a single
    // number.
    auto r2 = parse_int_list_("10");
    CRUCIBLE_TEST_REQUIRE(r2.size() == 1 && r2[0] == 10);

    CRUCIBLE_TEST_REQUIRE(parse_int_list_("").empty());
}

void test_singleton_identity() {
    auto& a = Topology::instance();
    auto& b = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(&a == &b);
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

void test_process_cpu_count_sane() {
    auto& t = Topology::instance();
    // A control group can only take cpus away, never add them, so the
    // count available to this process cannot exceed the machine's.
    CRUCIBLE_TEST_REQUIRE(t.process_cpu_count() <= t.num_smt_threads());
    // Equality is not asserted: the run may be inside a restricted
    // group, and that is a legitimate environment.
    CRUCIBLE_TEST_REQUIRE(t.process_cpu_count() >= 1);
}

void test_page_size_typical() {
    auto& t = Topology::instance();
    const auto ps = t.page_size_bytes();
    // Four kilobytes is the usual page on both supported
    // architectures, sixteen appears on some ARM configurations, and
    // sixty-four on a few others.  A machine outside this set is
    // possible but would want looking at.
    CRUCIBLE_TEST_REQUIRE(ps == 4096 || ps == 16384 || ps == 65536);
}

void test_cache_clusters_alias() {
    auto& t = Topology::instance();
    // The two names describe the same grouping in different vendor
    // vocabulary, so only the shape can differ if one drifts.
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

    // Sixty-four bytes on nearly every supported machine, with one
    // hundred and twenty-eight on some ARM parts.
    const auto line = t.cache_line_bytes();
    CRUCIBLE_TEST_REQUIRE(line == 32 || line == 64 || line == 128);
}

void test_smt_factor_sane() {
    auto& t = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(t.num_smt_threads() >= t.num_cores());
    const auto smt = t.smt_factor();
    // One thread per core where the feature is absent or disabled, two
    // on the common desktop and server parts, four on a few others.
    CRUCIBLE_TEST_REQUIRE(smt == 1 || smt == 2 || smt == 4);
}

void test_l3_groups_cover_threads() {
    auto& t = Topology::instance();
    if (t.source() == Topology::Source::Fallback) {
        // Nothing was probed, so there are no groups to check and only
        // the defaulted values remain.
        CRUCIBLE_TEST_REQUIRE(t.cores_per_socket() > 0);
        return;
    }
    const auto groups = t.l3_groups();
    CRUCIBLE_TEST_REQUIRE(!groups.empty());
    std::set<int> covered;
    for (auto const& g : groups) {
        CRUCIBLE_TEST_REQUIRE(!g.empty());
        for (int cpu : g)
            covered.insert(cpu);
    }
    CRUCIBLE_TEST_REQUIRE(covered.size() >= t.num_cores());
    // The groups list threads rather than cores, so the coverage sits
    // between the two counts and matches neither in general.
    CRUCIBLE_TEST_REQUIRE(covered.size() <= t.num_smt_threads());
}

void test_numa_self_distance_is_ten() {
    auto& t = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(t.numa_node_ids().size() == t.numa_nodes());
    for (const int node : t.numa_node_ids()) {
        // The kernel expresses distances relative to a self-distance
        // fixed at ten, so this value is a convention and not a
        // measurement.
        CRUCIBLE_TEST_REQUIRE(t.numa_distance(node, node) == 10);
    }
}

void test_numa_distance_symmetric() {
    auto& t = Topology::instance();
    // The distance matrix comes from firmware tables that are defined
    // to be symmetric, so an asymmetric pair means the parse went
    // wrong rather than that the machine is unusual.
    for (const int from : t.numa_node_ids()) {
        for (const int to : t.numa_node_ids()) {
            const auto forward = t.numa_distance(from, to);
            CRUCIBLE_TEST_REQUIRE(forward.has_value());
            CRUCIBLE_TEST_REQUIRE(forward == t.numa_distance(to, from));
        }
    }
}

void test_cores_on_node_nonempty() {
    auto& t = Topology::instance();
    std::set<int> all_cpus;
    for (const int node : t.numa_node_ids()) {
        const auto on_node = t.cores_on_node(node);
        if (t.source() == Topology::Source::Sysfs) {
            CRUCIBLE_TEST_REQUIRE(!on_node.empty());
        }
        for (int cpu : on_node)
            all_cpus.insert(cpu);
    }
    // Every thread belongs to exactly one node, so the union across
    // nodes accounts for all of them with nothing counted twice.
    if (t.source() == Topology::Source::Sysfs && !all_cpus.empty()) {
        CRUCIBLE_TEST_REQUIRE(all_cpus.size() == t.num_smt_threads());
    }
}

void test_source_consistency() {
    auto& t = Topology::instance();
    // Every getter answers under either source.  One reports what was
    // probed and the other reports defaults, and no caller has to know
    // which it got.
    const auto src = t.source();
    CRUCIBLE_TEST_REQUIRE(src == Topology::Source::Sysfs || src == Topology::Source::Fallback);

    // Printed so that a failure elsewhere in this file can be read
    // against the machine it ran on.
    std::fprintf(stderr,
                 "\n      Topology(source=%s):\n"
                 "        cores=%zu smt_threads=%zu smt_factor=%zu\n"
                 "        l1d=%zu KB l2=%zu KB l3=%zu MB line=%zu\n"
                 "        l3_groups=%zu numa_nodes=%zu cores_per_socket=%zu\n",
                 (src == Topology::Source::Sysfs ? "Sysfs" : "Fallback"), t.num_cores(), t.num_smt_threads(),
                 t.smt_factor(), t.l1d_per_core_bytes() / 1024, t.l2_per_core_bytes() / 1024,
                 t.l3_total_bytes() / (1024 * 1024), t.cache_line_bytes(), t.l3_groups().size(), t.numa_nodes(),
                 t.cores_per_socket());
}

// An id that names no node has no distance.  The answer is empty rather
// than the self-distance, so a caller cannot read an unknown node as
// local, and nothing indexes past the matrix.
void test_numa_distance_out_of_range() {
    auto& t = Topology::instance();
    const int first = t.numa_node_ids().front();
    CRUCIBLE_TEST_REQUIRE(!t.numa_distance(-1, first).has_value());
    CRUCIBLE_TEST_REQUIRE(!t.numa_distance(first, -1).has_value());
    CRUCIBLE_TEST_REQUIRE(!t.numa_distance(99999, first).has_value());
    CRUCIBLE_TEST_REQUIRE(!t.numa_distance(first, 99999).has_value());
}

void test_cores_on_node_out_of_range() {
    auto& t = Topology::instance();
    CRUCIBLE_TEST_REQUIRE(t.cores_on_node(-1).empty());
    CRUCIBLE_TEST_REQUIRE(t.cores_on_node(99999).empty());
}

// A sysfs tree in a temporary directory, removed when the object goes
// out of scope.  It holds only the files the NUMA probe reads.
class FakeSysfsTree {
public:
    explicit FakeSysfsTree(std::string_view name)
        : root_{std::filesystem::temp_directory_path()
                / (std::string{name} + "_" + std::to_string(static_cast<long>(::getpid())))} {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::create_directories(root_ / "devices/system/cpu");
        std::filesystem::create_directories(root_ / "devices/system/node");
    }

    FakeSysfsTree(const FakeSysfsTree&) = delete("the tree owns one directory on disk");
    FakeSysfsTree& operator=(const FakeSysfsTree&) = delete("the tree owns one directory on disk");

    ~FakeSysfsTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    void add_cpus(int count) const {
        for (int cpu = 0; cpu < count; ++cpu) {
            std::filesystem::create_directories(root_ / ("devices/system/cpu/cpu" + std::to_string(cpu)));
        }
    }

    void add_node(int node, std::string_view cpulist, std::string_view distance) const {
        const auto dir = root_ / ("devices/system/node/node" + std::to_string(node));
        std::filesystem::create_directories(dir);
        write(dir / "cpulist", cpulist);
        write(dir / "distance", distance);
    }

    // A file beside the node directories, as the kernel writes "online"
    // and "possible" there.  The probe must not read it as a node.
    void add_node_dir_file(std::string_view name, std::string_view contents) const {
        write(root_ / "devices/system/node" / std::string{name}, contents);
    }

    [[nodiscard]] std::string root() const { return root_.string(); }

private:
    static void write(const std::filesystem::path& path, std::string_view contents) {
        std::ofstream out{path};
        out << contents << '\n';
    }

    std::filesystem::path root_;
};

[[nodiscard]] bool span_equals(std::span<const int> got, std::initializer_list<int> want) {
    return std::equal(got.begin(), got.end(), want.begin(), want.end());
}

// A host with node0 and node2 and no node1.  Each accessor takes the id,
// so node 2 answers for the CPUs and the distances of node2, and node 1
// names nothing.  The sysfs distance row lists the distance to each online
// node in ascending order of node id, so the row of node2 is "21 10".
void test_sparse_numa_node_ids() {
    const FakeSysfsTree tree{"crucible_topology_sparse_nodes"};
    tree.add_cpus(4);
    tree.add_node(0, "0-1", "10 21");
    tree.add_node(2, "2-3", "21 10");
    tree.add_node_dir_file("online", "0,2");
    tree.add_node_dir_file("possible", "0-3");

    const auto ctx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>{eff::testing::test()};
    const auto topo = Topology::probe_tree(ctx, tree.root());

    CRUCIBLE_TEST_REQUIRE(topo.source() == Topology::Source::Sysfs);
    CRUCIBLE_TEST_REQUIRE(topo.numa_nodes() == 2);
    CRUCIBLE_TEST_REQUIRE(span_equals(topo.numa_node_ids(), {0, 2}));
    CRUCIBLE_TEST_REQUIRE(span_equals(topo.cores_on_node(0), {0, 1}));
    CRUCIBLE_TEST_REQUIRE(span_equals(topo.cores_on_node(2), {2, 3}));
    CRUCIBLE_TEST_REQUIRE(topo.cores_on_node(1).empty());
    CRUCIBLE_TEST_REQUIRE(topo.cores_on_node(3).empty());

    CRUCIBLE_TEST_REQUIRE(topo.numa_distance(0, 0) == 10);
    CRUCIBLE_TEST_REQUIRE(topo.numa_distance(0, 2) == 21);
    CRUCIBLE_TEST_REQUIRE(topo.numa_distance(2, 0) == 21);
    CRUCIBLE_TEST_REQUIRE(topo.numa_distance(2, 2) == 10);
    // An id that names no node has no distance, and it does not read as
    // local.
    CRUCIBLE_TEST_REQUIRE(!topo.numa_distance(1, 0).has_value());
    CRUCIBLE_TEST_REQUIRE(!topo.numa_distance(0, 1).has_value());
    CRUCIBLE_TEST_REQUIRE(!topo.numa_distance(1, 1).has_value());
}

// A distance row whose length is not the node count is malformed, and it
// reads as the conventional row: 10 to the node itself and 20 elsewhere,
// placed by position.  The node here has id 2, which is past the end of a
// two-entry row, so a row indexed by id would lose the self-distance.
void test_malformed_distance_row_reads_conventional() {
    const FakeSysfsTree tree{"crucible_topology_malformed_row"};
    tree.add_cpus(2);
    tree.add_node(0, "0", "10 20");
    tree.add_node(2, "1", "10");

    const auto ctx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>{eff::testing::test()};
    const auto topo = Topology::probe_tree(ctx, tree.root());

    CRUCIBLE_TEST_REQUIRE(topo.numa_distance(2, 2) == 10);
    CRUCIBLE_TEST_REQUIRE(topo.numa_distance(2, 0) == 20);
    CRUCIBLE_TEST_REQUIRE(topo.numa_distance(0, 2) == 20);
}

// A CPU with no cache directory keeps the fallback cache sizes, and the
// probe still reads the NUMA nodes.
void test_missing_cache_directory_still_reads_nodes() {
    const FakeSysfsTree tree{"crucible_topology_no_cache"};
    tree.add_cpus(2);
    tree.add_node(0, "0-1", "10");

    const auto ctx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>{eff::testing::test()};
    const auto topo = Topology::probe_tree(ctx, tree.root());

    CRUCIBLE_TEST_REQUIRE(topo.source() == Topology::Source::Sysfs);
    CRUCIBLE_TEST_REQUIRE(topo.num_smt_threads() == 2);
    CRUCIBLE_TEST_REQUIRE(span_equals(topo.cores_on_node(0), {0, 1}));
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_topology:\n");

    run_test("test_parse_size_suffix", test_parse_size_suffix);
    run_test("test_parse_cpu_list", test_parse_cpu_list);
    run_test("test_parse_int_list", test_parse_int_list);
    run_test("test_singleton_identity", test_singleton_identity);
    run_test("test_basic_positive_values", test_basic_positive_values);
    run_test("test_process_cpu_count_sane", test_process_cpu_count_sane);
    run_test("test_page_size_typical", test_page_size_typical);
    run_test("test_cache_clusters_alias", test_cache_clusters_alias);
    run_test("test_log_summary_emits", test_log_summary_emits);
    run_test("test_cache_hierarchy_monotonic", test_cache_hierarchy_monotonic);
    run_test("test_smt_factor_sane", test_smt_factor_sane);
    run_test("test_l3_groups_cover_threads", test_l3_groups_cover_threads);
    run_test("test_numa_self_distance_is_ten", test_numa_self_distance_is_ten);
    run_test("test_numa_distance_symmetric", test_numa_distance_symmetric);
    run_test("test_cores_on_node_nonempty", test_cores_on_node_nonempty);
    run_test("test_source_consistency", test_source_consistency);
    run_test("test_numa_distance_out_of_range", test_numa_distance_out_of_range);
    run_test("test_cores_on_node_out_of_range", test_cores_on_node_out_of_range);
    run_test("test_sparse_numa_node_ids", test_sparse_numa_node_ids);
    run_test("test_malformed_distance_row_reads_conventional", test_malformed_distance_row_reads_conventional);
    run_test("test_missing_cache_directory_still_reads_nodes", test_missing_cache_directory_still_reads_nodes);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
