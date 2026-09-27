// The pipeline router against running the same stages in order on the
// calling thread.  The small pipeline fits one core's private cache, so the
// router runs it inline and must cost no more than the direct run.  The
// large pipeline does not fit, so the router gives each stage a thread and
// must beat the direct run.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/Topology.h>

#include <foundation/effects/Ctx.h>

#include "bench_harness.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace cc = fixy::concurrent;

namespace pipeline_dispatch_bench {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;
constexpr std::size_t kLargeStageBytes = 20 * MiB;
constexpr std::size_t kLargeStages = 5;
constexpr std::size_t kLargeStageWords = kLargeStageBytes / sizeof(std::uint64_t);

constexpr fixy::HotFgCtx kForeground = foundation::effects::testing::foreground();
// The router starts threads, so the context that runs it must own Bg.
constexpr fixy::BgDrainCtx kCoordinator{foundation::effects::testing::bg()};

// Handles that never block and carry the working set that the router reads.
// The stages chain, so each handle names the same channel.
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

std::array<std::uint64_t, 1024> small_data{};
std::vector<std::uint64_t> large_data;
std::uint64_t small_sink = 0;

struct alignas(64) SinkCell {
    std::uint64_t value = 0;
};

static_assert(sizeof(SinkCell) == 64);
static_assert(alignof(SinkCell) == 64);

std::array<SinkCell, kLargeStages> large_sinks{};

// A multiply-xorshift chain over each word, so a stage does work in
// proportion to the bytes it touches.  O(words * Rounds).
template <std::size_t Rounds, std::uint64_t Seed, std::uint64_t Multiplier, unsigned Shift, std::uint64_t Increment>
[[nodiscard]] static std::uint64_t mix_words(std::span<const std::uint64_t> words) noexcept {
    std::uint64_t acc = Seed;
    for (const std::uint64_t word : words) {
        std::uint64_t x = word ^ acc;
        for (std::size_t round = 0; round < Rounds; ++round) {
            x = (x * Multiplier) ^ (x >> Shift);
        }
        acc ^= x + Increment;
    }
    return acc;
}

// A small stage mixes Count words of small_data from Begin.
template <std::size_t Ws, std::size_t Begin, std::size_t Count>
static void small_stage(Consumer<Ws>&&, Producer<Ws>&&) noexcept {
    static_assert(Begin + Count <= std::tuple_size_v<decltype(small_data)>);
    small_sink ^= mix_words<4, 0x9E3779B97F4A7C15ULL, 0xD1B54A32D192ED03ULL, 27, 0x94D049BB133111EBULL>(
        std::span<const std::uint64_t>{small_data}.subspan(Begin, Count));
    bench::do_not_optimize(small_sink);
}

// Large stage Stage mixes its own 20 MiB slice of large_data.
template <std::size_t Stage>
static void large_stage(Consumer<10 * MiB>&&, Producer<10 * MiB>&&) noexcept {
    static_assert(Stage < kLargeStages);
    large_sinks[Stage].value ^= mix_words<8, 0x243F6A8885A308D3ULL, 0x9E3779B97F4A7C15ULL, 29, 0xD1B54A32D192ED03ULL>(
        std::span<const std::uint64_t>{large_data}.subspan(Stage * kLargeStageWords, kLargeStageWords));
    bench::do_not_optimize(large_sinks[Stage].value);
}

using SmallStages = std::tuple<cc::Stage<&small_stage<1 * KiB, 0, 256>, fixy::HotFgCtx>,
                               cc::Stage<&small_stage<1536, 256, 384>, fixy::HotFgCtx>,
                               cc::Stage<&small_stage<1536, 640, 384>, fixy::HotFgCtx>>;

template <std::size_t... Stage>
auto large_stage_list(std::index_sequence<Stage...>) -> std::tuple<cc::Stage<&large_stage<Stage>, fixy::HotFgCtx>...>;

using LargeStages = decltype(large_stage_list(std::make_index_sequence<kLargeStages>{}));

template <class Stage, class List>
inline constexpr bool is_listed_v = false;

template <class Stage, class... Listed>
inline constexpr bool is_listed_v<Stage, std::tuple<Listed...>> = (std::same_as<Stage, Listed> || ...);

// The stages of this bench, and no other stage, opt in to the inline run.
template <class Stage>
concept IsBenchStage = is_listed_v<Stage, SmallStages> || is_listed_v<Stage, LargeStages>;

}  // namespace pipeline_dispatch_bench

namespace fixy::concurrent {

template <class Stage>
    requires pipeline_dispatch_bench::IsBenchStage<Stage>
struct is_stage_inline_safe<Stage> : std::true_type {};

}  // namespace fixy::concurrent

namespace pipeline_dispatch_bench {

template <class List>
struct pipeline_of;

template <class... Stages>
struct pipeline_of<std::tuple<Stages...>> {
    using type = cc::Pipeline<Stages...>;
};

using SmallPipeline = typename pipeline_of<SmallStages>::type;
using LargePipeline = typename pipeline_of<LargeStages>::type;

static_assert(SmallPipeline::aggregate_per_call_working_set == 8 * KiB);
static_assert(LargePipeline::aggregate_per_call_working_set == 100 * MiB);
static_assert(SmallPipeline::inline_safe);
static_assert(LargePipeline::inline_safe);

template <class Stage>
[[nodiscard]] static auto mint_bench_stage() noexcept {
    return cc::mint_stage<Stage::fn_ptr>(kForeground, typename Stage::consumer_handle_type{},
                                         typename Stage::producer_handle_type{});
}

// Each stage of the list, in order, on the calling thread.
template <class... Stages>
static void run_in_order(std::type_identity<std::tuple<Stages...>>) noexcept {
    (mint_bench_stage<Stages>().run(), ...);
}

// The same stages through the router, which picks the inline run or a
// thread per stage.
template <class... Stages>
static void run_through_router(std::type_identity<std::tuple<Stages...>>) noexcept {
    cc::mint_pipeline(kCoordinator, mint_bench_stage<Stages>()...).run(kCoordinator);
}

static void fill_inputs() {
    for (std::size_t i = 0; i < small_data.size(); ++i) {
        small_data[i] = i * 1315423911ULL;
    }
    large_data.resize(kLargeStageWords * kLargeStages);
    for (std::size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = i * 11400714819323198485ULL;
    }
}

}  // namespace pipeline_dispatch_bench

int main() {
    using namespace pipeline_dispatch_bench;

    fill_inputs();
    bench::print_system_info();

    std::printf("pipeline dispatch facts: small=%zuB inline=%d large=%zuB inline=%d cpus=%zu\n",
                SmallPipeline::aggregate_per_call_working_set, SmallPipeline::will_run_inline() ? 1 : 0,
                LargePipeline::aggregate_per_call_working_set, LargePipeline::will_run_inline() ? 1 : 0,
                cc::Topology::instance().process_cpu_count());

    using Small = std::type_identity<SmallStages>;
    using Large = std::type_identity<LargeStages>;
    std::array reports{
        bench::Run{"pipeline.small.direct"}.samples(2000).warmup(200).no_pin().max_wall_ms(1000).measure(
            [] { run_in_order(Small{}); }),
        bench::Run{"pipeline.small.router"}.samples(2000).warmup(200).no_pin().max_wall_ms(1000).measure(
            [] { run_through_router(Small{}); }),
        bench::Run{"pipeline.large.direct"}.samples(12).warmup(1).no_pin().max_wall_ms(3000).measure(
            [] { run_in_order(Large{}); }),
        bench::Run{"pipeline.large.router"}.samples(12).warmup(1).no_pin().max_wall_ms(3000).measure(
            [] { run_through_router(Large{}); }),
    };
    bench::emit_reports(reports, bench::env_json());

    const double small_ratio = reports[1].pct.p50 / reports[0].pct.p50;
    const double large_speedup = reports[2].pct.p50 / reports[3].pct.p50;
    std::printf("\nchecks: small_router/direct=%.3fx large_direct/router=%.3fx\n", small_ratio, large_speedup);
    return 0;
}
