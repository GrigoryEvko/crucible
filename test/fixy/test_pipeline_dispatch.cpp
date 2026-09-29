// The dispatch decision, run for real: a pipeline whose working set fits
// one core's private cache runs every stage on the calling thread, and
// one whose working set does not runs one thread per stage.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Topology.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include <sched.h>

namespace cc = fixy::concurrent;

namespace pipeline_dispatch_test {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

// The stages chain, so each handle names the same channel.  Its working
// set does not change which channel it acts on.
struct IntChannel {};

template <std::size_t Ws>
struct Consumer {
    using channel_type = IntChannel;
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct Producer {
    using channel_type = IntChannel;
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

std::thread::id main_thread;
std::atomic<int> calls{0};
std::atomic<int> main_thread_calls{0};
std::atomic<int> non_main_thread_calls{0};

static void reset_counters() noexcept {
    calls.store(0, std::memory_order_relaxed);
    main_thread_calls.store(0, std::memory_order_relaxed);
    non_main_thread_calls.store(0, std::memory_order_relaxed);
}

static void record_call() noexcept {
    calls.fetch_add(1, std::memory_order_relaxed);
    if (std::this_thread::get_id() == main_thread) {
        main_thread_calls.fetch_add(1, std::memory_order_relaxed);
    } else {
        non_main_thread_calls.fetch_add(1, std::memory_order_relaxed);
    }
}

static void small_a(Consumer<1 * KiB>&&, Producer<1 * KiB>&&) noexcept { record_call(); }

static void small_b(Consumer<1536>&&, Producer<1536>&&) noexcept { record_call(); }

static void small_c(Consumer<1536>&&, Producer<1536>&&) noexcept { record_call(); }

static void large(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept { record_call(); }

using SmallA = cc::Stage<&small_a, fixy::HotFgCtx>;
using SmallB = cc::Stage<&small_b, fixy::HotFgCtx>;
using SmallC = cc::Stage<&small_c, fixy::HotFgCtx>;
using Large = cc::Stage<&large, fixy::HotFgCtx>;

static_assert(cc::stage_per_call_ws_v<SmallA> == 2 * KiB);
static_assert(cc::stage_per_call_ws_v<SmallB> == 3 * KiB);
static_assert(cc::stage_per_call_ws_v<SmallC> == 3 * KiB);
static_assert(cc::aggregate_per_call_ws_v<SmallA, SmallB, SmallC> == 8 * KiB);
static_assert(cc::aggregate_per_call_ws_v<Large, Large, Large, Large, Large> == 100 * MiB);

}  // namespace pipeline_dispatch_test

namespace fixy::concurrent {

template <>
struct is_stage_inline_safe<pipeline_dispatch_test::SmallA> : std::true_type {};

template <>
struct is_stage_inline_safe<pipeline_dispatch_test::SmallB> : std::true_type {};

template <>
struct is_stage_inline_safe<pipeline_dispatch_test::SmallC> : std::true_type {};

template <>
struct is_stage_inline_safe<pipeline_dispatch_test::Large> : std::true_type {};

}  // namespace fixy::concurrent

namespace pipeline_dispatch_test {

using SmallPipeline = cc::Pipeline<SmallA, SmallB, SmallC>;
using LargePipeline = cc::Pipeline<Large, Large, Large, Large, Large>;

static_assert(SmallPipeline::aggregate_per_call_working_set == 8 * KiB);
static_assert(LargePipeline::aggregate_per_call_working_set == 100 * MiB);
static_assert(SmallPipeline::inline_safe);
static_assert(LargePipeline::inline_safe);

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

static void test_small_pipeline_runs_inline() {
    main_thread = std::this_thread::get_id();
    reset_counters();

    require(SmallPipeline::will_run_inline(), "8KB inline-safe pipeline should select inline dispatch");

    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    const fixy::BgDrainCtx coordinator{::foundation::effects::testing::bg()};
    auto s0 = cc::mint_stage<&small_a>(ctx, Consumer<1 * KiB>{}, Producer<1 * KiB>{});
    auto s1 = cc::mint_stage<&small_b>(ctx, Consumer<1536>{}, Producer<1536>{});
    auto s2 = cc::mint_stage<&small_c>(ctx, Consumer<1536>{}, Producer<1536>{});
    auto p = cc::mint_pipeline(coordinator, std::move(s0), std::move(s1), std::move(s2));
    std::move(p).run(coordinator);

    require(calls.load(std::memory_order_relaxed) == 3, "small pipeline should run all three stages");
    require(main_thread_calls.load(std::memory_order_relaxed) == 3,
            "small inline pipeline should run only on caller thread");
    require(non_main_thread_calls.load(std::memory_order_relaxed) == 0,
            "small inline pipeline unexpectedly spawned a worker thread");
}

static void test_large_pipeline_spawns_threads() {
    main_thread = std::this_thread::get_id();
    reset_counters();

    require(!LargePipeline::will_run_inline(), "100MB inline-safe pipeline should exceed private-L2 inline gate");

    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    const fixy::BgDrainCtx coordinator{::foundation::effects::testing::bg()};
    auto s0 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s1 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s2 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s3 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s4 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto p = cc::mint_pipeline(coordinator, std::move(s0), std::move(s1), std::move(s2), std::move(s3), std::move(s4));
    std::move(p).run(coordinator);

    require(calls.load(std::memory_order_relaxed) == 5, "large pipeline should run all five stages");
    require(main_thread_calls.load(std::memory_order_relaxed) == 0,
            "large pipeline should not execute stage bodies on caller thread");
    require(non_main_thread_calls.load(std::memory_order_relaxed) == 5,
            "large pipeline should spawn one worker thread per stage");
}

// The stages below run under a background context, so the pipeline pins the
// thread of each one.  Each body records the CPU that its thread runs on
// after the pin.
constexpr std::size_t kPinnedStages = 2;
std::array<std::atomic<int>, kPinnedStages> pinned_cpus{};
std::atomic<std::size_t> pinned_calls{0};

static void pinned_body(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    const std::size_t slot = pinned_calls.fetch_add(1, std::memory_order_acq_rel);
    if (slot < kPinnedStages) pinned_cpus[slot].store(::sched_getcpu(), std::memory_order_release);
}

// A caller keeps its threads off a CPU through its affinity mask, and each
// thread that the pipeline starts obeys the same mask.  The test narrows the
// mask of the calling thread to one CPU, which is not one of the first CPUs
// of the largest cache cluster.  A CPU table that ignores the mask pins the
// stages to those first CPUs.  A process with fewer than three CPUs cannot
// tell the two apart, and the test then only checks that each stage ran.
static void test_threaded_stages_stay_in_the_caller_mask() {
    cpu_set_t before;
    CPU_ZERO(&before);
    require(::sched_getaffinity(0, sizeof(before), &before) == 0, "cannot read the affinity mask of the caller");

    std::vector<int> allowed;
    for (std::size_t cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &before)) allowed.push_back(static_cast<int>(cpu));
    }
    require(!allowed.empty(), "the affinity mask of the caller is empty");

    // The CPUs that a table which ignores the mask gives the two stages.
    std::vector<int> mask_blind;
    for (auto const& cluster : cc::Topology::instance().cache_clusters()) {
        if (cluster.size() > mask_blind.size()) mask_blind = cluster;
    }
    mask_blind.resize(std::min(mask_blind.size(), kPinnedStages));

    int target = -1;
    for (auto cpu = allowed.rbegin(); cpu != allowed.rend(); ++cpu) {
        if (std::find(mask_blind.begin(), mask_blind.end(), *cpu) == mask_blind.end()) {
            target = *cpu;
            break;
        }
    }
    const bool can_tell_apart = allowed.size() >= 3 && target >= 0;
    if (target < 0) target = allowed.front();

    cpu_set_t narrow;
    CPU_ZERO(&narrow);
    CPU_SET(static_cast<std::size_t>(target), &narrow);
    require(::sched_setaffinity(0, sizeof(narrow), &narrow) == 0, "cannot narrow the affinity mask of the caller");

    pinned_calls.store(0, std::memory_order_release);
    const fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto s0 = cc::mint_stage<&pinned_body>(bg, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s1 = cc::mint_stage<&pinned_body>(bg, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto p = cc::mint_pipeline(bg, std::move(s0), std::move(s1));
    std::move(p).run(bg);

    require(::sched_setaffinity(0, sizeof(before), &before) == 0, "cannot restore the affinity mask of the caller");
    require(pinned_calls.load(std::memory_order_acquire) == kPinnedStages, "each pinned stage should run once");
    if (!can_tell_apart) return;
    for (std::size_t i = 0; i < kPinnedStages; ++i) {
        require(pinned_cpus[i].load(std::memory_order_acquire) == target,
                "a stage thread ran outside the affinity mask of the caller");
    }
}

}  // namespace pipeline_dispatch_test

int main() {
    pipeline_dispatch_test::test_small_pipeline_runs_inline();
    pipeline_dispatch_test::test_large_pipeline_spawns_threads();
    pipeline_dispatch_test::test_threaded_stages_stay_in_the_caller_mask();
    std::fprintf(stderr, "test_pipeline_dispatch: ALL PASSED\n");
    return EXIT_SUCCESS;
}
