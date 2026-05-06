#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/ExecCtx.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <type_traits>

namespace cc = crucible::concurrent;
namespace eff = crucible::effects;

namespace pipeline_dispatch_test {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

template <std::size_t Ws>
struct Consumer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct Producer {
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

static void small_a(Consumer<1 * KiB>&&, Producer<1 * KiB>&&) noexcept {
    record_call();
}

static void small_b(Consumer<1536>&&, Producer<1536>&&) noexcept {
    record_call();
}

static void small_c(Consumer<1536>&&, Producer<1536>&&) noexcept {
    record_call();
}

static void large(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    record_call();
}

using SmallA = cc::Stage<&small_a, eff::HotFgCtx>;
using SmallB = cc::Stage<&small_b, eff::HotFgCtx>;
using SmallC = cc::Stage<&small_c, eff::HotFgCtx>;
using Large = cc::Stage<&large, eff::HotFgCtx>;

static_assert(cc::stage_per_call_ws_v<SmallA> == 2 * KiB);
static_assert(cc::stage_per_call_ws_v<SmallB> == 3 * KiB);
static_assert(cc::stage_per_call_ws_v<SmallC> == 3 * KiB);
static_assert(cc::aggregate_per_call_ws_v<SmallA, SmallB, SmallC>
              == 8 * KiB);
static_assert(cc::aggregate_per_call_ws_v<Large, Large, Large, Large, Large>
              == 100 * MiB);

}  // namespace pipeline_dispatch_test

namespace crucible::concurrent {

template <>
struct stage_inline_safe<pipeline_dispatch_test::SmallA> : std::true_type {};

template <>
struct stage_inline_safe<pipeline_dispatch_test::SmallB> : std::true_type {};

template <>
struct stage_inline_safe<pipeline_dispatch_test::SmallC> : std::true_type {};

template <>
struct stage_inline_safe<pipeline_dispatch_test::Large> : std::true_type {};

}  // namespace crucible::concurrent

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

    require(SmallPipeline::will_run_inline(),
            "8KB inline-safe pipeline should select inline dispatch");

    eff::HotFgCtx ctx{};
    auto s0 = cc::mint_stage<&small_a>(ctx, Consumer<1 * KiB>{}, Producer<1 * KiB>{});
    auto s1 = cc::mint_stage<&small_b>(ctx, Consumer<1536>{}, Producer<1536>{});
    auto s2 = cc::mint_stage<&small_c>(ctx, Consumer<1536>{}, Producer<1536>{});
    auto p = cc::mint_pipeline(ctx, std::move(s0), std::move(s1), std::move(s2));
    std::move(p).run();

    require(calls.load(std::memory_order_relaxed) == 3,
            "small pipeline should run all three stages");
    require(main_thread_calls.load(std::memory_order_relaxed) == 3,
            "small inline pipeline should run only on caller thread");
    require(non_main_thread_calls.load(std::memory_order_relaxed) == 0,
            "small inline pipeline unexpectedly spawned a worker thread");
}

static void test_large_pipeline_spawns_threads() {
    main_thread = std::this_thread::get_id();
    reset_counters();

    require(!LargePipeline::will_run_inline(),
            "100MB inline-safe pipeline should exceed private-L2 inline gate");

    eff::HotFgCtx ctx{};
    auto s0 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s1 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s2 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s3 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s4 = cc::mint_stage<&large>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto p = cc::mint_pipeline(
        ctx,
        std::move(s0),
        std::move(s1),
        std::move(s2),
        std::move(s3),
        std::move(s4));
    std::move(p).run();

    require(calls.load(std::memory_order_relaxed) == 5,
            "large pipeline should run all five stages");
    require(main_thread_calls.load(std::memory_order_relaxed) == 0,
            "large pipeline should not execute stage bodies on caller thread");
    require(non_main_thread_calls.load(std::memory_order_relaxed) == 5,
            "large pipeline should spawn one worker thread per stage");
}

}  // namespace pipeline_dispatch_test

int main() {
    pipeline_dispatch_test::test_small_pipeline_runs_inline();
    pipeline_dispatch_test::test_large_pipeline_spawns_threads();
    std::fprintf(stderr, "test_pipeline_dispatch: ALL PASSED\n");
    return EXIT_SUCCESS;
}
