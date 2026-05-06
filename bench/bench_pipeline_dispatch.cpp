#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/effects/ExecCtx.h>

#include "bench_harness.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace cc = crucible::concurrent;
namespace eff = crucible::effects;

namespace pipeline_dispatch_bench {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;
constexpr std::size_t kLargeStageBytes = 20 * MiB;
constexpr std::size_t kLargeStages = 5;
constexpr std::size_t kLargeWords = (kLargeStageBytes * kLargeStages)
                                  / sizeof(std::uint64_t);

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

std::array<std::uint64_t, 1024> small_data{};
std::vector<std::uint64_t> large_data;
std::uint64_t small_sink = 0;

struct alignas(64) SinkCell {
    std::uint64_t value = 0;
};

static_assert(sizeof(SinkCell) == 64);
static_assert(alignof(SinkCell) == 64);

std::array<SinkCell, kLargeStages> large_sinks{};

static void touch_words(std::span<const std::uint64_t> words) noexcept {
    std::uint64_t acc = 0;
    for (std::uint64_t v : words) {
        acc += (v * 0x9E3779B97F4A7C15ULL) ^ (acc >> 7);
    }
    small_sink ^= acc;
    bench::do_not_optimize(small_sink);
}

[[nodiscard]] static std::uint64_t
touch_words_compute(std::span<const std::uint64_t> words) noexcept {
    std::uint64_t acc = 0x243F6A8885A308D3ULL;
    for (std::uint64_t v : words) {
        std::uint64_t x = v ^ acc;
        for (std::size_t r = 0; r < 8; ++r) {
            x = (x * 0x9E3779B97F4A7C15ULL) ^ (x >> 29);
        }
        acc ^= x + 0xD1B54A32D192ED03ULL;
    }
    return acc;
}

static void touch_large_stage(std::size_t stage) noexcept {
    const std::size_t words_per_stage = kLargeStageBytes / sizeof(std::uint64_t);
    const std::size_t begin = stage * words_per_stage;
    const std::uint64_t acc = touch_words_compute(std::span<const std::uint64_t>{
        large_data.data() + begin,
        words_per_stage});
    large_sinks[stage].value ^= acc;
    bench::do_not_optimize(large_sinks[stage].value);
}

static void small_a(Consumer<1 * KiB>&&, Producer<1 * KiB>&&) noexcept {
    touch_words(std::span<const std::uint64_t>{small_data.data(), 256});
}

static void small_b(Consumer<1536>&&, Producer<1536>&&) noexcept {
    touch_words(std::span<const std::uint64_t>{small_data.data() + 256, 384});
}

static void small_c(Consumer<1536>&&, Producer<1536>&&) noexcept {
    touch_words(std::span<const std::uint64_t>{small_data.data() + 640, 384});
}

static void large_0(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    touch_large_stage(0);
}
static void large_1(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    touch_large_stage(1);
}
static void large_2(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    touch_large_stage(2);
}
static void large_3(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    touch_large_stage(3);
}
static void large_4(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    touch_large_stage(4);
}

using SmallA = cc::Stage<&small_a, eff::HotFgCtx>;
using SmallB = cc::Stage<&small_b, eff::HotFgCtx>;
using SmallC = cc::Stage<&small_c, eff::HotFgCtx>;
using Large0 = cc::Stage<&large_0, eff::HotFgCtx>;
using Large1 = cc::Stage<&large_1, eff::HotFgCtx>;
using Large2 = cc::Stage<&large_2, eff::HotFgCtx>;
using Large3 = cc::Stage<&large_3, eff::HotFgCtx>;
using Large4 = cc::Stage<&large_4, eff::HotFgCtx>;

static void run_small_direct() noexcept {
    small_a(Consumer<1 * KiB>{}, Producer<1 * KiB>{});
    small_b(Consumer<1536>{}, Producer<1536>{});
    small_c(Consumer<1536>{}, Producer<1536>{});
}

static void run_large_direct() noexcept {
    large_0(Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    large_1(Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    large_2(Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    large_3(Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    large_4(Consumer<10 * MiB>{}, Producer<10 * MiB>{});
}

static void fill_inputs() {
    for (std::size_t i = 0; i < small_data.size(); ++i) {
        small_data[i] = i * 1315423911ULL;
    }
    large_data.resize(kLargeWords);
    for (std::size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = i * 11400714819323198485ULL;
    }
}

}  // namespace pipeline_dispatch_bench

namespace crucible::concurrent {

template <> struct stage_inline_safe<::pipeline_dispatch_bench::SmallA> : std::true_type {};
template <> struct stage_inline_safe<::pipeline_dispatch_bench::SmallB> : std::true_type {};
template <> struct stage_inline_safe<::pipeline_dispatch_bench::SmallC> : std::true_type {};
template <> struct stage_inline_safe<::pipeline_dispatch_bench::Large0> : std::true_type {};
template <> struct stage_inline_safe<::pipeline_dispatch_bench::Large1> : std::true_type {};
template <> struct stage_inline_safe<::pipeline_dispatch_bench::Large2> : std::true_type {};
template <> struct stage_inline_safe<::pipeline_dispatch_bench::Large3> : std::true_type {};
template <> struct stage_inline_safe<::pipeline_dispatch_bench::Large4> : std::true_type {};

}  // namespace crucible::concurrent

namespace pipeline_dispatch_bench {

using SmallPipeline = cc::Pipeline<SmallA, SmallB, SmallC>;
using LargePipeline = cc::Pipeline<Large0, Large1, Large2, Large3, Large4>;

static_assert(SmallPipeline::aggregate_per_call_working_set == 8 * KiB);
static_assert(LargePipeline::aggregate_per_call_working_set == 100 * MiB);
static_assert(SmallPipeline::inline_safe);
static_assert(LargePipeline::inline_safe);

static void run_small_pipeline() noexcept {
    eff::HotFgCtx ctx{};
    auto s0 = cc::mint_stage<&small_a>(ctx, Consumer<1 * KiB>{}, Producer<1 * KiB>{});
    auto s1 = cc::mint_stage<&small_b>(ctx, Consumer<1536>{}, Producer<1536>{});
    auto s2 = cc::mint_stage<&small_c>(ctx, Consumer<1536>{}, Producer<1536>{});
    auto p = cc::mint_pipeline(ctx, std::move(s0), std::move(s1), std::move(s2));
    std::move(p).run();
}

static void run_large_pipeline() noexcept {
    eff::HotFgCtx ctx{};
    auto s0 = cc::mint_stage<&large_0>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s1 = cc::mint_stage<&large_1>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s2 = cc::mint_stage<&large_2>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s3 = cc::mint_stage<&large_3>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto s4 = cc::mint_stage<&large_4>(ctx, Consumer<10 * MiB>{}, Producer<10 * MiB>{});
    auto p = cc::mint_pipeline(
        ctx,
        std::move(s0),
        std::move(s1),
        std::move(s2),
        std::move(s3),
        std::move(s4));
    std::move(p).run();
}

}  // namespace pipeline_dispatch_bench

int main() {
    using namespace pipeline_dispatch_bench;

    fill_inputs();
    bench::print_system_info();

    std::printf("pipeline dispatch facts: small=%zuB inline=%d large=%zuB inline=%d cpus=%zu\n",
                SmallPipeline::aggregate_per_call_working_set,
                SmallPipeline::will_run_inline() ? 1 : 0,
                LargePipeline::aggregate_per_call_working_set,
                LargePipeline::will_run_inline() ? 1 : 0,
                cc::Topology::instance().process_cpu_count());

    auto small_direct = bench::Run{"pipeline.small.direct"}
        .samples(2'000)
        .warmup(200)
        .no_pin()
        .max_wall_ms(1'000)
        .measure(run_small_direct);
    auto small_pipeline = bench::Run{"pipeline.small.router"}
        .samples(2'000)
        .warmup(200)
        .no_pin()
        .max_wall_ms(1'000)
        .measure(run_small_pipeline);
    auto large_direct = bench::Run{"pipeline.large.direct"}
        .samples(12)
        .warmup(1)
        .no_pin()
        .max_wall_ms(3'000)
        .measure(run_large_direct);
    auto large_pipeline = bench::Run{"pipeline.large.router"}
        .samples(12)
        .warmup(1)
        .no_pin()
        .max_wall_ms(3'000)
        .measure(run_large_pipeline);

    std::array reports{
        std::move(small_direct),
        std::move(small_pipeline),
        std::move(large_direct),
        std::move(large_pipeline),
    };
    bench::emit_reports(reports, bench::env_json());

    const double small_ratio = reports[1].pct.p50 / reports[0].pct.p50;
    const double large_speedup = reports[2].pct.p50 / reports[3].pct.p50;
    std::printf("\nchecks: small_router/direct=%.3fx large_direct/router=%.3fx\n",
                small_ratio,
                large_speedup);
    return 0;
}
