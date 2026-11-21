// The suite runs unprivileged, so it cannot verify that SCHED_DEADLINE,
// the frequency lock or the C-state disable took effect.  What it can
// check is that mint_hardening() warns and continues rather than failing
// when a privilege is missing.  That is why several assertions below
// accept either outcome.

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#endif

#include <crucible/warden/Hardening.h>
#include <crucible/warden/Policy.h>
#include <crucible/warden/Registry.h>
#include <crucible/warden/CpuTopology.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionFork.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/NumaPlacement.h>
#include <fixy/os/Pages.h>
#include <fixy/os/Sched.h>
#include <foundation/Linear.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Region.h>
#include <foundation/os/Auxiliary.h>

#include "padding_bytes.h"
#include <fixy/Core.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

int failures = 0;

// The topology queries read sysfs and procfs, and a policy is startup
// work, so the suite holds the startup load context, which owns Init,
// IO and Block.
constexpr ::fixy::InitLoadCtx kStartup{::foundation::effects::testing::init()};

// The size of a page of this host.
[[nodiscard]] std::uint64_t page_size() noexcept {
    return ::foundation::os::read_auxiliary(::foundation::os::AuxiliaryEntry::page_size)
        .expect("the auxiliary vector gives the size of a page");
}

// THE FRESH PAGES.  A case that counts the pages with no frame takes an
// anonymous mapping of the kernel.  Each page holds zero, and no page has a
// frame before a write or a populate.  No allocator touches the mapping, so
// this holds in each build: the allocator of ThreadSanitizer writes each
// byte of a zeroed block, and AddressSanitizer writes the first page of a
// block.
using IoBlockCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Test,
    ::foundation::effects::Row<::foundation::effects::Effect::Test, ::foundation::effects::Effect::Alloc,
                               ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>>;

// The owner of the fresh pages of a case.
struct FreshPages final {
    using permission_row = ::foundation::effects::Row<>;
};

using WriteAnon = ::fixy::atom::mmap::with_prot<::fixy::mmap::prot::WriteCopy>;
using Anonymous = ::fixy::atom::mmap::with_share<::fixy::mmap::share::Anonymous>;

// The count of the fresh pages of one mapping.
constexpr std::size_t kFreshPageCount = 8;

// The fresh pages of a case (THE FRESH PAGES), for the permission of the
// case.
template <class Owner>
[[nodiscard]] auto map_fresh_pages(Owner const& owner) {
    IoBlockCtx const ctx{::foundation::effects::testing::test()};
    return ::fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, kFreshPageCount * page_size())
        .expect("an anonymous mapping of the fresh pages works")
        .consume();
}

// The count bytes from the byte at offset of the bytes of a mapping.
[[nodiscard]] ::fixy::View<std::byte const> window_of(::fixy::View<std::byte const> bytes, std::size_t offset,
                                                      std::size_t count) noexcept {
    return bytes.window(offset, count).expect("the window fits in the mapping");
}

// The count of the pages of bytes that have no frame.  A page that nothing
// wrote has no frame, and a page that a read only touched maps the shared
// zero page, which has no node either.
[[nodiscard]] std::size_t pages_with_no_frame(::fixy::View<std::byte const> bytes) noexcept {
    return ::fixy::numa::census_page_nodes(kStartup, bytes)
        .expect("the kernel gives the node of each page")
        .pages_without_node();
}

// True when the test runs under the emulator of a cross build, qemu user mode.
constexpr bool kIsUnderEmulator = CRUCIBLE_TEST_UNDER_EMULATOR != 0;

// True when the kernel takes the populate advice, which needs Linux 5.14, and
// faults in the page, which qemu user mode does not do.  The probe is the
// second page of a mapping that no code wrote.
[[nodiscard]] bool kernel_populates_pages() noexcept {
    auto owner = ::foundation::permissions::mint_permission_root<FreshPages>();
    bool is_populated = false;
    {
        auto mapping = map_fresh_pages(owner);
        ::fixy::View<std::byte const> const fresh = mapping.view<std::byte const>().expect("a mapping holds bytes");
        is_populated = ::fixy::pages::populate_pages(kStartup, window_of(fresh, page_size(), page_size())).is_ok();
    }
    ::foundation::linear::drop(std::move(owner));
    return is_populated;
}

// Words that the program only reads.  The linker puts a constant with no
// relocation in a segment with no write access, so a write fault there is
// refused.
[[nodiscard]] consteval ::fixy::FixedArray<std::uint64_t, 4096> make_read_only_words() noexcept {
    ::fixy::FixedArray<std::uint64_t, 4096> words{};
    words.with_mut([](auto run) noexcept {
        std::uint64_t value = 1;
        for (std::uint64_t& word : run) {
            word = value;
            ++value;
        }
    });
    return words;
}

constexpr ::fixy::FixedArray<std::uint64_t, 4096> kReadOnlyWords = make_read_only_words();

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            ::fixy::report(::fixy::Sink::Err, "FAIL {}:{}  {} — {}\n", ::fixy::TextView{__FILE__}, __LINE__, \
                           ::fixy::TextView{#cond}, ::fixy::TextView{msg});                                  \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)

void test_topology_basic() {
    using namespace crucible::warden;

    const int n = num_online_cpus(kStartup);
    CHECK(n >= 1, "must see at least one online CPU");

    const auto allowed = allowed_cpus(kStartup);
    CHECK(!allowed.is_empty(), "cpuset must grant at least one CPU");
    for (const int c : allowed) {
        CHECK(c >= 0 && c < n + 1024, "allowed CPU out of plausible range");
    }

    const auto iso = isolated_cpus(kStartup);
    // The isolated set is legitimately empty on most systems, so only
    // the shape of an entry can be asserted.
    for (const int c : iso) {
        CHECK(c >= 0, "isolcpu non-negative");
    }

    // The result is discarded: the claim is only that probing a
    // non-hybrid CPU is safe.
    for (const int c : allowed) {
        (void)is_p_core(kStartup, c);
    }

    if (!allowed.is_empty()) {
        const int c = allowed.first().expect("the set is not empty");
        const auto sibs = smt_siblings(kStartup, c);
        if (!sibs.is_empty()) {
            CHECK(sibs.contains(c), "thread_siblings must include the core itself");
        }
    }
}

// allowed_cpus gives the affinity of the calling thread.  A worker that
// narrowed its own affinity to one CPU gets that one CPU back, and not the
// affinity of the main thread.
void test_allowed_cpus_reads_the_calling_thread() {
    using namespace crucible::warden;

#ifdef __linux__
    const auto allowed = allowed_cpus(kStartup);
    // With one CPU, the worker and the main thread have the same affinity.
    if (allowed.size() < 2) return;

    // The worker narrows its affinity through the gated door.  It ends
    // after the read, so it drops the prior affinity and does not restore.
    const int chosen_cpu = allowed.last().expect("the set holds two CPUs");
    bool is_narrowed = false;
    crucible::warden::CpuSet worker_allowed{};
    std::jthread worker{[&is_narrowed, &worker_allowed, chosen_cpu] {
        const auto prior = ::fixy::sched::apply_affinity_to_cpu(kStartup, chosen_cpu);
        is_narrowed = prior.is_ok();
        worker_allowed = allowed_cpus(kStartup);
    }};
    worker.join();

    CHECK(is_narrowed, "the worker narrows its own affinity");
    CHECK(worker_allowed.size() == 1 && worker_allowed.contains(chosen_cpu),
          "allowed_cpus gives the affinity of the calling thread");
    CHECK(allowed_cpus(kStartup) == allowed, "the worker leaves the affinity of the main thread as it was");
#endif
}

// The set of the given CPU ids.
constexpr crucible::warden::CpuSet cpus_of(std::initializer_list<std::size_t> ids) {
    crucible::warden::CpuSet set{};
    for (const std::size_t id : ids)
        set.insert(id);
    return set;
}

// The input strings are the shapes the kernel writes into the sysfs and
// procfs cpulist files: a range, an explicit list, several ranges, and
// an empty file.
void test_cpulist_parser() {
    using namespace crucible::warden::detail;

    // The same parse in a constant evaluation.
    static_assert(parse_cpulist("0,1,2,3") == cpus_of({0, 1, 2, 3}));
    static_assert(parse_cpus_allowed_list("Name:\tx\nCpus_allowed_list:\t0,1,2,3\nMems:\t0\n")
                  == cpus_of({0, 1, 2, 3}));

    CHECK(parse_cpulist("0-3").size() == 4, "range 0-3 → 4 elements");
    CHECK(parse_cpulist("0,1,2,3").size() == 4, "explicit list 0-3");
    CHECK(parse_cpulist("0-3,8-11").size() == 8, "two ranges");
    CHECK(parse_cpulist("").is_empty(), "empty input → empty");
    CHECK(parse_cpulist("   ").is_empty(), "whitespace → empty");
    const auto v = parse_cpulist("3,1,2,0,3,1");
    CHECK(v == cpus_of({0, 1, 2, 3}), "dedupe + sort");
    CHECK(parse_cpulist("\t0-1 , 3 ") == cpus_of({0, 1, 3}), "spaces and tabs around an entry");
    CHECK(parse_cpus_allowed_list("Name:\tx\nCpus_allowed_list:\t0-2\nMems:\t0\n") == cpus_of({0, 1, 2}),
          "the list is read from its line of /proc/thread-self/status");
}

// Text that is not a CPU list gives an empty list, and no text can make
// the list longer than the kernel ceiling.  The first four inputs are the
// ones a fuzz campaign found: a range that expanded to millions of ids, a
// range that wrapped the loop counter, a number past the range of int, and
// a list that ends in a digit, which the parser read past its end.
void test_cpulist_parser_refuses_malformed_text() {
    using namespace crucible::warden::detail;

    CHECK(parse_cpulist("0-8191911").is_empty(), "a range past the ceiling");
    CHECK(parse_cpulist("0-2147483647").is_empty(), "a range that wraps an int");
    CHECK(parse_cpulist("99999999999999999999").is_empty(), "a number past the range of int");
    ::fixy::FixedText<2> no_terminator{};
    CHECK(::fixy::format(no_terminator, "12").is_ok(), "the two digits fit the text");
    CHECK(parse_cpulist(no_terminator.view()) == cpus_of({12}),
          "a list that ends in a digit, with no terminator after it");
    CHECK(parse_cpulist("5-2").is_empty(), "a range whose end is below its start");
    CHECK(parse_cpulist("0-3,x").is_empty(), "an entry that is not a number");
    CHECK(parse_cpulist("-1").is_empty(), "a negative id");
    CHECK(parse_cpulist("3-").is_empty(), "a range with no end");
    CHECK(parse_cpulist("8192").is_empty(), "an id at the ceiling");
    CHECK(parse_cpulist("8191").size() == 1, "the largest id below the ceiling");
    // The parse of the Report family reads a 0x prefix, and the parser of a
    // CPU list refuses it, as a kernel list never holds one.
    CHECK(parse_cpulist("0x5").is_empty(), "a hexadecimal id");
    CHECK(parse_cpulist("0-0x8").is_empty(), "a range with a hexadecimal end");

    CHECK(parse_cpulist("0-8191,0-8191,0-8191,0-8191").size() == kMaxCpuCount,
          "a repeated full range stays at the ceiling");
}

void test_core_selector(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

    CoreSelector sel;
    sel.prefer_isolcpu = true;
    sel.prefer_p_core = true;
    sel.avoid_smt_sibling = true;

    const int pick = select_hot_cpu(kStartup, topology, sel);
    const auto allowed = allowed_cpus(kStartup);
    CHECK(pick >= 0, "select_hot_cpu must return something on a normal system");
    CHECK(allowed.contains(pick), "picked CPU must be in allowed set");

    if (!allowed.is_empty()) {
        CoreSelector sel_explicit;
        sel_explicit.explicit_cpu = allowed.last().expect("the set is not empty");
        CHECK(select_hot_cpu(kStartup, topology, sel_explicit) == sel_explicit.explicit_cpu,
              "explicit_cpu must be honored when allowed");
    }
}

// cpu 0 absorbs timer interrupts and RCU callbacks on most Linux
// configurations, so its cache state is the least predictable member of
// the allowed set.  That is the reason the default selector steers away
// from it.
void test_core_selector_avoids_cpu0(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

    const auto allowed = allowed_cpus(kStartup);
    // The heuristic is only exercisable when cpu 0 is in the cpuset and
    // at least one other CPU exists to steer towards.  A runner inside a
    // cpu-constrained cgroup legitimately takes the skip.
    const bool have0 = allowed.contains(0);
    if (!have0 || allowed.size() < 2) return;

    CoreSelector sel;  // default: avoid_cpu0 = true
    const int pick = select_hot_cpu(kStartup, topology, sel);
    CHECK(pick >= 0, "select_hot_cpu returns something");
    CHECK(pick != 0, "default selector avoids cpu0 when another CPU is available");

    // With every scoring signal flattened the lowest-index tie-break is
    // all that remains, so opting out of the cpu0 rule must return cpu0.
    CoreSelector sel_legacy;
    sel_legacy.avoid_cpu0 = false;
    sel_legacy.prefer_p_core = false;  // flatten any P/E signal
    sel_legacy.prefer_isolcpu = false;  // ignore isolcpus
    const int pick_legacy = select_hot_cpu(kStartup, topology, sel_legacy);
    CHECK(pick_legacy == 0, "avoid_cpu0=false with flat scoring returns cpu0");
}

void test_core_selector_avoids_exclude(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

    const auto allowed = allowed_cpus(kStartup);
    if (allowed.size() < 2) {
        // A single-CPU cpuset leaves nowhere to steer to, which
        // satisfies the exclude path trivially.
        return;
    }

    CoreSelector sel;
    sel.prefer_isolcpu = false;  // don't let iso pool skew the pick
    sel.prefer_p_core = false;
    sel.avoid_smt_sibling = false;  // exclude is what we're testing

    const int excluded = allowed.first().expect("the set holds two CPUs");
    CpuSet exclude{};
    exclude.insert(static_cast<std::size_t>(excluded));
    const int pick = select_hot_cpu(kStartup, topology, sel, exclude);

    CHECK(pick >= 0, "select_hot_cpu must still return a CPU when one is excluded");
    CHECK(pick != excluded, "select_hot_cpu must not return a CPU that was in the exclude list");
    CHECK(allowed.contains(pick), "pick is still drawn from the allowed set");
}

// The companions are allowed CPUs other than the hot CPU, and their count is
// the smaller of the count asked for and the count of the other CPUs.
void test_warm_cpus_leave_out_the_hot_cpu(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

    const CpuSet allowed = allowed_cpus(kStartup);
    const int first = allowed.first().value_or(-1);
    const ::fixy::Option<CpuId> hot_cpu =
        allowed.first().map([](int cpu) noexcept { return CpuId{static_cast<std::uint32_t>(cpu)}; });
    const std::size_t others = hot_cpu.is_some() ? allowed.size() - 1 : allowed.size();

    const CpuSet warm = select_warm_cpus(kStartup, topology, hot_cpu, ::fixy::ElementCount{2});
    CHECK(!warm.contains(first), "the hot CPU is not a companion");
    CHECK(warm.intersection(allowed) == warm, "each companion is an allowed CPU");
    CHECK(warm.size() == (others < 2 ? others : 2), "the count of the companions is the count asked for");

    CHECK(select_warm_cpus(kStartup, topology, hot_cpu, ::fixy::ElementCount{0}).is_empty(),
          "a count of zero gives no companion");
    CHECK(select_warm_cpus(kStartup, topology, ::fixy::none, ::fixy::ElementCount{allowed.size()}) == allowed,
          "with no hot CPU, each allowed CPU can be a companion");
}

// The warden reads the NUMA node of a CPU from the topology that its owner
// lends, and not from the sysfs of the host.  The planted tree of two
// sockets (test/CMakeLists.txt) puts CPUs 0 and 1 on node 0, CPUs 2 and 3 on
// node 1, and CPUs 4 and 5 on node 3.  The selector reads no other fact of
// the host: no isolated CPU, no core type, no sibling and no rule for cpu0.
void test_choice_reads_the_node_of_the_lent_topology() {
    using namespace crucible::warden;

    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::topology::Topology const planted = ::fixy::topology::Topology::probe_tree(test, TWO_PACKAGE_SYSFS_ROOT);
    TopologyRef const lent = ::fixy::erase_brand(::fixy::to(planted));
    CpuSet allowed{};
    allowed.insert_range(0, 5);

    CpuSet const same_node =
        detail::select_warm_cpus_of(allowed, lent, ::fixy::some(CpuId{2}), ::fixy::ElementCount{1});
    CHECK(same_node.size() == 1 && same_node.contains(3), "the one companion of CPU 2 is CPU 3 of node 1");
    CpuSet const three = detail::select_warm_cpus_of(allowed, lent, ::fixy::some(CpuId{4}), ::fixy::ElementCount{3});
    CHECK(three.size() == 3 && three.contains(5) && three.contains(0) && three.contains(1),
          "CPU 5 of node 3 comes first, then the lowest CPUs of the other nodes");

    CoreSelector sel{};
    sel.prefer_isolcpu = false;
    sel.prefer_p_core = false;
    sel.avoid_smt_sibling = false;
    sel.avoid_cpu0 = false;
    sel.numa_hint = 3;
    auto const allowed_side = ::fixy::side_as<crucible::warden::side::AllowedCpus>(allowed);
    auto const no_exclusion = ::fixy::side_as<crucible::warden::side::ExcludedCpus>(CpuSet{});
    CHECK(detail::select_hot_cpu_of(allowed_side, lent, sel, no_exclusion) == 4,
          "the hint of node 3 picks CPU 4, the lowest CPU of node 3");
    sel.numa_hint = 2;
    CHECK(detail::select_hot_cpu_of(allowed_side, lent, sel, no_exclusion) == 0,
          "a hint of a node that the topology does not name gives no bonus");
}

void test_policy_none_is_noop(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

#ifdef __linux__
    cpu_set_t before{};
    CPU_ZERO(&before);
    (void)::sched_getaffinity(0, sizeof(before), &before);
#endif

    auto g = mint_hardening(kStartup, topology, Policy::none());
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

void test_policy_dev_quiet_pins_and_reverts(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

#ifdef __linux__
    cpu_set_t before{};
    CPU_ZERO(&before);
    CHECK(::sched_getaffinity(0, sizeof(before), &before) == 0, "baseline sched_getaffinity works");

    const int before_count = CPU_COUNT(&before);
#endif

    {
        auto g = mint_hardening(kStartup, topology, Policy::dev_quiet());
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
void test_revert_clears_observers(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

#ifdef __linux__
    // Explicit early revert.
    {
        auto g = mint_hardening(kStartup, topology, Policy::dev_quiet());
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
        auto lhs = mint_hardening(kStartup, topology, Policy::dev_quiet());
        auto rhs = mint_hardening(kStartup, topology, Policy::dev_quiet());
        lhs = std::move(rhs);
        // NOLINTNEXTLINE(bugprone-use-after-move) — reading the
        // moved-from object is the point: a disarmed one must report
        // not-applied for every observer.
        CHECK(!rhs.scheduler_applied(), "moved-from rhs must NOT report scheduler_applied");
        CHECK(!rhs.affinity_applied(), "moved-from rhs must NOT report affinity_applied");
        CHECK(rhs.regions_locked() == 0, "moved-from rhs must report zero locked regions");
        CHECK(rhs.pinned_cpu() == -1, "moved-from rhs must report pinned_cpu == -1");
    }
#endif
}

void test_policy_production_degrades_gracefully(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

    // This policy asks for SCHED_DEADLINE, CAP_SYS_NICE, a frequency
    // lock and a C-state disable, most of which are absent here.  The
    // claim is only that mint_hardening() returns instead of failing.
    Policy p = Policy::production();
    p.on_missing_capability = OnMissingCap::DegradeAndWarn;

    auto g = mint_hardening(kStartup, topology, p);

    CHECK(g.pinned_cpu() >= -1, "pinned_cpu is plausible after production()");
}

// The region of a snapshot that starts at the first byte of bytes, or no
// region.
[[nodiscard]] ::fixy::Option<crucible::warden::HotRegion> region_at(::fixy::View<std::byte const> bytes) noexcept {
    auto const first = bytes.get(0).expect("the View holds a first byte");
    for (crucible::warden::HotRegion const& region : crucible::warden::HotRegionRegistry::instance().snapshot()) {
        auto const start = region.bytes.get(0);
        for (auto const start_byte : start) {
            if (::fixy::same_object(start_byte, first)) return ::fixy::some(region);
        }
    }
    return ::fixy::none;
}

// The length of the region of a snapshot that starts at the first byte of
// bytes, or zero for no region.
[[nodiscard]] size_t registered_length(::fixy::View<std::byte const> bytes) noexcept {
    return region_at(bytes).match([](crucible::warden::HotRegion region) noexcept { return region.bytes.size(); },
                                  []() noexcept { return size_t{0}; });
}

// A region is known by its first byte.  A second registration at the same
// first byte replaces the length and the hint, a withdrawal at a different
// first byte changes nothing, and a withdrawal of a region keeps each other
// region with its own length.
void test_registry_basic() {
    using namespace crucible::warden;

    auto& reg = HotRegionRegistry::instance();
    const size_t baseline = reg.size();

    alignas(64)::fixy::FixedArray<std::byte, 4096> buf{};
    ::fixy::View<std::byte const> const bytes = buf.view();
    register_hot_region(bytes, /*huge_hint=*/false);
    CHECK(reg.size() == baseline + 1, "one registration bumps size");

    register_hot_region(bytes, /*huge_hint=*/true);
    CHECK(reg.size() == baseline + 1, "re-register same addr is an update, not a new slot");
    CHECK(registered_length(bytes) == 4096, "len preserved on update");
    CHECK(region_at(bytes).match([](HotRegion region) noexcept { return region.huge_hint; },
                                 []() noexcept { return false; }),
          "huge_hint updated to true");

    register_hot_region(bytes.window(0, 1024).expect("the window fits"), /*huge_hint=*/false);
    CHECK(reg.size() == baseline + 1, "a shorter region at the same first byte replaces the region");
    CHECK(registered_length(bytes) == 1024, "the length of the second registration is the length");

    register_hot_region(::fixy::View<std::byte const>{}, /*huge_hint=*/false);
    CHECK(reg.size() == baseline + 1, "an empty region registers nothing");

    unregister_hot_region(bytes.window(1, 16).expect("the window fits"));
    CHECK(reg.size() == baseline + 1, "a withdrawal at a different first byte changes nothing");

    ::fixy::View<std::byte const> const second = bytes.window(1024, 1024).expect("the window fits");
    ::fixy::View<std::byte const> const third = bytes.window(2048, 2048).expect("the window fits");
    register_hot_region(second, /*huge_hint=*/false);
    register_hot_region(third, /*huge_hint=*/false);
    unregister_hot_region(bytes);
    CHECK(reg.size() == baseline + 2, "unregister withdraws one region");
    CHECK(registered_length(bytes) == 0, "the withdrawn region is not in the snapshot");
    CHECK(registered_length(second) == 1024 && registered_length(third) == 2048,
          "each other region keeps its own length");

    unregister_hot_region(second);
    unregister_hot_region(third);
    CHECK(reg.size() == baseline, "unregister restores size");

    unregister_hot_region(bytes);
    CHECK(reg.size() == baseline, "double-unregister is a no-op");
}

// Whether the mlock took effect is unobservable without CAP_IPC_LOCK,
// so the claim is narrower: mint_hardening() walks the registry and
// teardown leaves the registry at the size it started from.
void test_registry_applies_on_mint_hardening(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;

    alignas(64)::fixy::FixedArray<std::byte, 4096> buf{};
    ::fixy::View<std::byte const> const bytes = buf.view();
    const size_t baseline = HotRegionRegistry::instance().size();

    register_hot_region(bytes, /*huge_hint=*/false);
    CHECK(HotRegionRegistry::instance().size() == baseline + 1, "buf registered");

    // This policy enables the region lock while staying on SCHED_OTHER,
    // so the walk runs without needing CAP_SYS_NICE.
    {
        auto g = mint_hardening(kStartup, topology, Policy::dev_quiet());
        // The count is nonzero only where CAP_IPC_LOCK or memlock
        // headroom exists, so it carries no claim here.
        (void)g.regions_locked();
    }

    unregister_hot_region(bytes);
    CHECK(HotRegionRegistry::instance().size() == baseline, "registry clean after unregister");
}

// The tag of the two threads of the race of the registry.  The split of
// fixy/concurrent/PermissionedChannel.h gives one permission to each.
struct RegistryRaceTag final {};
using RaceWhole = ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, RegistryRaceTag>;
using RaceWriter = ::fixy::concurrent::channel_tag::Producer<::fixy::concurrent::ChannelForm::wire, RegistryRaceTag>;
using RaceReader = ::fixy::concurrent::channel_tag::Consumer<::fixy::concurrent::ChannelForm::wire, RegistryRaceTag>;
using RaceCtx = ::foundation::effects::ExecCtx<::foundation::effects::Bg,
                                               ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

// The turns of the writer of the race.  Each turn registers and withdraws
// two regions.
constexpr std::uint32_t kRaceTurns = 20000;

// True when the two Views start at one byte.
[[nodiscard]] bool is_same_start(::fixy::View<std::byte const> first, ::fixy::View<std::byte const> second) noexcept {
    auto const first_start = first.get(0);
    auto const second_start = second.get(0);
    for (auto const first_byte : first_start) {
        for (auto const second_byte : second_start) {
            return ::fixy::same_object(first_byte, second_byte);
        }
    }
    return false;
}

// A snapshot pairs each first byte with the length of its own registration.
// The writer registers a short region and withdraws it, then a long region at
// a different first byte, in each turn.  The reader takes snapshots at the
// same time.  A region with the first byte of one and the length of the other
// is a torn read.  ThreadSanitizer also checks the order that the lock of the
// registry gives to the two threads.
void test_snapshot_pairs_each_start_with_its_own_length() {
    using namespace crucible::warden;
    alignas(64)::fixy::FixedArray<std::byte, 8192> buf{};
    ::fixy::View<std::byte const> const all = buf.view();
    ::fixy::View<std::byte const> const short_region = all.window(0, 64).expect("the window fits");
    ::fixy::View<std::byte const> const long_region = all.window(4096, 4096).expect("the window fits");
    ::fixy::Atomic<bool> is_written{false};
    ::fixy::Atomic<std::uint64_t> torn_reads{0};

    auto const write_turns = [&]() noexcept {
        for (std::uint32_t turn = 0; turn < kRaceTurns; ++turn) {
            register_hot_region(short_region, /*huge_hint=*/false);
            unregister_hot_region(short_region);
            register_hot_region(long_region, /*huge_hint=*/false);
            unregister_hot_region(long_region);
        }
        is_written.store_release(true);
    };
    auto const read_snapshots = [&]() noexcept {
        while (!is_written.load_acquire()) {
            for (HotRegion const& region : HotRegionRegistry::instance().snapshot()) {
                bool const is_torn_short = is_same_start(region.bytes, short_region) && region.bytes.size() != 64;
                bool const is_torn_long = is_same_start(region.bytes, long_region) && region.bytes.size() != 4096;
                if (is_torn_short || is_torn_long) static_cast<void>(torn_reads.fetch_add_acq_rel(1));
            }
        }
    };

    auto whole = ::foundation::permissions::mint_permission_root<RaceWhole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    auto rebuilt = ::fixy::concurrent::mint_permission_fork<RaceWriter, RaceReader>(
        RaceCtx{::foundation::effects::testing::bg()}, std::move(whole),
        [&write_turns](::foundation::permissions::WriteView<RaceWriter, WholeBrand> const&, RaceCtx const&) noexcept {
            write_turns();
        },
        [&read_snapshots](::foundation::permissions::WriteView<RaceReader, WholeBrand> const&,
                          RaceCtx const&) noexcept { read_snapshots(); });
    ::foundation::linear::drop(std::move(rebuilt));
    CHECK(torn_reads.load_acquire() == 0, "no snapshot pairs a first byte with the length of a different region");
}

// A snapshot returns up to 256 regions, and the registry holds a list of
// 256 regions.  Each padding byte of a region costs one store for each
// element of the list (padding_bytes.h).
void test_region_has_no_padding_byte() { crucible::test::expect_no_padding_byte<^^crucible::warden::HotRegion>(kTest); }

// prefault faults in each page that holds a byte of its range.  The window
// below holds the last byte of the first page and the first byte of the
// second, and a loop that steps one page from the first byte of the window
// reaches only the first page.  The third page stays with no frame.  A
// range that the process cannot write gives false, and the process goes on:
// a loop that writes a byte of each page ends the process there.
void test_prefault_faults_in_each_page_of_the_range() {
    using namespace crucible::warden;
    if (!kernel_populates_pages()) {
        ::fixy::report(::fixy::Sink::Err,
                       "test_prefault_faults_in_each_page_of_the_range: SKIPPED (no populate: Linux before 5.14 "
                       "or an emulator)\n");
        return;
    }
    // A huge page would give a frame to the pages around the range, so the
    // process refuses huge pages in this case.
    ::fixy::pages::ThpSetting const first_thp =
        ::fixy::pages::exchange_thp_setting(kStartup, ::fixy::pages::ThpSetting::refused)
            .expect("the process refuses THP");
    std::size_t const page = page_size();
    auto owner = ::foundation::permissions::mint_permission_root<FreshPages>();
    {
        auto mapping = map_fresh_pages(owner);
        ::fixy::View<std::byte const> const fresh = mapping.view<std::byte const>().expect("a mapping holds bytes");
        CHECK(pages_with_no_frame(window_of(fresh, page, 3 * page)) == 3, "no page of a new mapping has a frame");

        CHECK(Hardening::prefault(kStartup, window_of(fresh, (2 * page) - 1, 2)),
              "the prefault of a writable range works");
        CHECK(pages_with_no_frame(window_of(fresh, page, 2 * page)) == 0,
              "each page that holds a byte of the range has a frame");
        CHECK(pages_with_no_frame(window_of(fresh, 3 * page, page)) == 1, "the page after the range has no frame");
    }
    ::foundation::linear::drop(std::move(owner));

    CHECK(!Hardening::prefault(kStartup, ::foundation::core::bytes(kReadOnlyWords)),
          "the prefault of a read-only range gives false");
    static_cast<void>(::fixy::pages::exchange_thp_setting(kStartup, first_thp).expect("the process puts back THP"));
}

// apply with prefault_hot_regions faults in each page of each registered
// region.  The region holds the last byte of one page and the first byte of
// the next, and the page after it keeps no frame.  The lock and the huge page
// knobs are off, so no other step faults in a page.  A policy with the knob
// off leaves each page of the region with no frame.
void test_apply_prefaults_each_registered_region(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;
    if (!kernel_populates_pages()) {
        ::fixy::report(::fixy::Sink::Err,
                       "test_apply_prefaults_each_registered_region: SKIPPED (no populate: Linux before 5.14 or an "
                       "emulator)\n");
        return;
    }
    ::fixy::pages::ThpSetting const first_thp =
        ::fixy::pages::exchange_thp_setting(kStartup, ::fixy::pages::ThpSetting::refused)
            .expect("the process refuses THP");
    std::size_t const page = page_size();
    auto owner = ::foundation::permissions::mint_permission_root<FreshPages>();
    {
        auto mapping = map_fresh_pages(owner);
        ::fixy::View<std::byte> const bytes = mapping.view<std::byte>().expect("a mapping holds bytes");
        ::fixy::View<std::byte const> const region =
            bytes.window((2 * page) - 1, 2).expect("the region fits in the mapping");
        register_hot_region(region, /*huge_hint=*/false);

        Policy policy = Policy::dev_quiet();
        policy.mlock_hot_regions = false;
        policy.thp_hint_pools = false;
        policy.thp_collapse_now = false;
        policy.prefault_hot_regions = false;
        {
            AppliedPolicy const applied = mint_hardening(kStartup, topology, policy);
            CHECK(applied.regions_locked() == 0, "the policy locks no region");
        }
        CHECK(pages_with_no_frame(window_of(bytes, page, 3 * page)) == 3,
              "a policy with the knob off gives no frame to a page of the region");

        policy.prefault_hot_regions = true;
        {
            AppliedPolicy const applied = mint_hardening(kStartup, topology, policy);
            CHECK(applied.regions_locked() == 0, "the policy locks no region");
        }
        CHECK(pages_with_no_frame(window_of(bytes, page, 2 * page)) == 0, "each page of the region has a frame");
        CHECK(pages_with_no_frame(window_of(bytes, 3 * page, page)) == 1, "the page after the region has no frame");

        unregister_hot_region(region);
    }
    ::foundation::linear::drop(std::move(owner));
    static_cast<void>(::fixy::pages::exchange_thp_setting(kStartup, first_thp).expect("the process puts back THP"));
}

// revert puts back the THP setting that the process had before apply.  A
// process that refused THP before apply still refuses it after the revert,
// and the revert does not allow THP for it.
void test_revert_keeps_a_prior_thp_refusal(crucible::warden::TopologyRef topology) {
    using namespace crucible::warden;
    ::foundation::core::Result<::fixy::pages::ThpSetting, ::foundation::os::Errno> const exchanged =
        ::fixy::pages::exchange_thp_setting(kStartup, ::fixy::pages::ThpSetting::refused);
    bool const is_refused_knob = exchanged.match(
        [](::fixy::pages::ThpSetting) noexcept { return false; },
        [](::foundation::os::Errno error) noexcept { return error == ::foundation::os::Errno::einval; });
    if (is_refused_knob && kIsUnderEmulator) {
        ::fixy::report(::fixy::Sink::Err, "test_revert_keeps_a_prior_thp_refusal: SKIPPED (qemu-user)\n");
        return;
    }
    const ::fixy::pages::ThpSetting first =
        exchanged.match([](::fixy::pages::ThpSetting setting) noexcept { return setting; },
                        [](::foundation::os::Errno) noexcept -> ::fixy::pages::ThpSetting {
                            ::fixy::fatal("the process refuses THP before the apply");
                        });
    {
        Policy policy = Policy::dev_quiet();
        policy.disable_thp_global = true;
        AppliedPolicy const applied = mint_hardening(kStartup, topology, policy);
        CHECK(applied.pinned_cpu() >= -1, "the apply returns a handle");
    }
    const ::fixy::pages::ThpSetting after_revert =
        ::fixy::pages::exchange_thp_setting(kStartup, first).expect("the process puts back its first setting");
    CHECK(after_revert == ::fixy::pages::ThpSetting::refused,
          "the revert keeps the THP refusal that the process had before the apply");
}

}  // namespace

int main() {
    // The suite is the owner of the warden: it loads the topology of the
    // host one time and lends it to each choice of a CPU.
    ::fixy::topology::Topology const host = ::fixy::topology::Topology::load(kStartup);
    crucible::warden::TopologyRef const topology = ::fixy::erase_brand(::fixy::to(host));

    test_topology_basic();
    test_allowed_cpus_reads_the_calling_thread();
    test_cpulist_parser();
    test_cpulist_parser_refuses_malformed_text();
    test_core_selector(topology);
    test_core_selector_avoids_cpu0(topology);
    test_core_selector_avoids_exclude(topology);
    test_warm_cpus_leave_out_the_hot_cpu(topology);
    test_choice_reads_the_node_of_the_lent_topology();
    test_policy_none_is_noop(topology);
    test_policy_dev_quiet_pins_and_reverts(topology);
    test_revert_clears_observers(topology);
    test_policy_production_degrades_gracefully(topology);
    test_registry_basic();
    test_registry_applies_on_mint_hardening(topology);
    test_region_has_no_padding_byte();
    test_snapshot_pairs_each_start_with_its_own_length();
    test_prefault_faults_in_each_page_of_the_range();
    test_apply_prefaults_each_registered_region(topology);
    test_revert_keeps_a_prior_thp_refusal(topology);

    if (failures == 0) {
        ::fixy::check::pass(kTest, "test_warden_policy: OK\n");
        return 0;
    }
    ::fixy::report(::fixy::Sink::Err, "test_warden_policy: {} FAILURES\n", failures);
    return 1;
}
