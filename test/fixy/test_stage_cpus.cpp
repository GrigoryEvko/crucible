// Tests of the CPUs of the stages of fixy/concurrent/StageCpus.h, and of the
// pins of two threaded pipeline runs at the same time.
//
// The plans read a planted sysfs tree of eight CPUs in two cache clusters,
// cpu0 to cpu3 and cpu4 to cpu7, and each plan claims in a table of its own.
// The threaded test runs on the host and claims in the table of the process.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/StageCpus.h>
#include <fixy/os/Futex.h>
#include <fixy/os/Sched.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Topology.h>

#include <foundation/Platform.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Index.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace cc = ::fixy::concurrent;
namespace eff = ::foundation::effects;
using ::foundation::algebra::lattices::AffinityLattice;
using ::foundation::algebra::lattices::AffinityMask;
using ::foundation::core::Atomic;
using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::fixy::topology::Topology;

[[nodiscard]] Topology planted_topology() noexcept {
    const eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>> test_ctx{eff::testing::test()};
    return Topology::probe_tree(test_ctx, STAGE_CPUS_SYSFS_ROOT);
}

// The mask of the CPUs of a list.
template <std::size_t Count>
[[nodiscard]] AffinityMask mask_of(FixedArray<std::uint16_t, Count> const& cpus) noexcept {
    AffinityMask mask{};
    for (std::uint16_t const cpu : cpus) {
        mask = AffinityLattice::join(mask, AffinityMask::single(cpu));
    }
    return mask;
}

// Zero when the plan is the expected plan.  Else it reports each stage that
// differs and gives one.
template <std::size_t Stages>
[[nodiscard]] int expect_plan(::fixy::TextView what, ::fixy::View<int const, Stages> got,
                              FixedArray<int, Stages> const& want) noexcept {
    int failures = 0;
    for (Index<Stages> const stage : FixedArray<int, Stages>::indices()) {
        if (got.at(stage) == want.at(stage)) continue;
        ::fixy::report(::fixy::Sink::Err, "{}: stage {} got CPU {}, want CPU {}\n", what, stage.raw(), got.at(stage),
                       want.at(stage));
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}

// Zero when no CPU is in the two plans.  -1 is no CPU.
[[nodiscard]] int expect_disjoint(::fixy::TextView what, ::fixy::View<int const> first,
                                  ::fixy::View<int const> second) noexcept {
    for (int const left : first) {
        for (int const right : second) {
            if (left < 0 || left != right) continue;
            ::fixy::report(::fixy::Sink::Err, "{}: two stages of two live runs hold CPU {}\n", what, left);
            return 1;
        }
    }
    return 0;
}

// More stages than one cluster: the extra stages take the next cluster.
[[nodiscard]] int six_stages_take_the_next_cluster() noexcept {
    Topology const topology = planted_topology();
    cc::StageCpuClaims claims{};
    FixedArray<int, 6> const plan =
        ::fixy::build_fixed_array<int, 6>([&claims, clusters = topology.cache_clusters()](auto run) noexcept {
            cc::claim_stage_cpus(claims, AffinityMask::range(0, 7), clusters, std::move(run));
        });
    return expect_plan("six stages on two clusters of four CPUs", plan.view(), FixedArray<int, 6>{0, 1, 2, 3, 4, 5});
}

// More stages than CPUs: each CPU takes one stage, and each other stage runs
// unpinned.
[[nodiscard]] int stages_past_the_mask_run_unpinned() noexcept {
    Topology const topology = planted_topology();
    cc::StageCpuClaims claims{};
    FixedArray<int, 10> const plan =
        ::fixy::build_fixed_array<int, 10>([&claims, clusters = topology.cache_clusters()](auto run) noexcept {
            cc::claim_stage_cpus(claims, AffinityMask::range(0, 7), clusters, std::move(run));
        });
    return expect_plan("ten stages on eight CPUs", plan.view(), FixedArray<int, 10>{0, 1, 2, 3, 4, 5, 6, 7, -1, -1});
}

// Two runs at the same time: the second run takes no CPU that the first run
// holds.  After the first run gives its CPUs back, a third run takes them.
[[nodiscard]] int two_live_runs_share_no_cpu() noexcept {
    Topology const topology = planted_topology();
    cc::StageCpuClaims claims{};
    auto [first, first_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<int, 3>);
    auto [second, second_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<int, 3>);
    first.with_mut(first_key, [&claims, clusters = topology.cache_clusters()](auto run) noexcept {
        cc::claim_stage_cpus(claims, AffinityMask::range(0, 7), clusters, std::move(run));
    });
    second.with_mut(second_key, [&claims, clusters = topology.cache_clusters()](auto run) noexcept {
        cc::claim_stage_cpus(claims, AffinityMask::range(0, 7), clusters, std::move(run));
    });
    int failures = expect_disjoint("two runs of three stages at the same time", first.view(), second.view());
    failures += expect_plan("the first run", first.view(), FixedArray<int, 3>{0, 1, 2});
    failures += expect_plan("the second run", second.view(), FixedArray<int, 3>{3, 4, 5});

    first.with_mut(first_key, [&claims](auto run) noexcept { cc::give_back_stage_cpus(claims, std::move(run)); });
    auto [third, third_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<int, 3>);
    third.with_mut(third_key, [&claims, clusters = topology.cache_clusters()](auto run) noexcept {
        cc::claim_stage_cpus(claims, AffinityMask::range(0, 7), clusters, std::move(run));
    });
    failures += expect_plan("a run after the first run gave its CPUs back", third.view(), FixedArray<int, 3>{0, 1, 2});
    second.with_mut(second_key, [&claims](auto run) noexcept { cc::give_back_stage_cpus(claims, std::move(run)); });
    third.with_mut(third_key, [&claims](auto run) noexcept { cc::give_back_stage_cpus(claims, std::move(run)); });
    for (std::uint16_t cpu = 0; cpu < 8; ++cpu) {
        if (!claims.is_claimed(cpu)) continue;
        ::fixy::report(::fixy::Sink::Err, "CPU {} stays claimed after each run gave its CPUs back\n", cpu);
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}

// The cluster with the most CPUs of the mask comes first, and only CPUs of
// the mask are candidates.  An empty mask pins no stage.
[[nodiscard]] int the_mask_orders_the_clusters() noexcept {
    Topology const topology = planted_topology();
    cc::StageCpuClaims claims{};
    auto [plan, plan_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<int, 4>);
    plan.with_mut(plan_key, [&claims, clusters = topology.cache_clusters()](auto run) noexcept {
        cc::claim_stage_cpus(claims, mask_of(FixedArray<std::uint16_t, 4>{1, 4, 5, 6}), clusters, std::move(run));
    });
    int failures =
        expect_plan("a mask with three CPUs of the second cluster", plan.view(), FixedArray<int, 4>{4, 5, 6, 1});
    plan.with_mut(plan_key, [&claims](auto run) noexcept { cc::give_back_stage_cpus(claims, std::move(run)); });

    FixedArray<int, 2> const empty_plan =
        ::fixy::build_fixed_array<int, 2>([&claims, clusters = topology.cache_clusters()](auto run) noexcept {
            cc::claim_stage_cpus(claims, AffinityMask{}, clusters, std::move(run));
        });
    failures += expect_plan("an empty mask", empty_plan.view(), FixedArray<int, 2>{-1, -1});
    return failures == 0 ? 0 : 1;
}

// ── Two threaded runs at the same time, on the host ──────────────────────

constexpr std::size_t KiB = 1024;
constexpr std::size_t kStagesOfRun = 12;
constexpr std::size_t kStagesOfTwoRuns = 2 * kStagesOfRun;

// The stages chain, so each handle names one channel.  The working set is
// past each private cache, so the pipeline starts one thread for each stage.
struct PinChannel {};

struct PinConsumer {
    using channel_type = PinChannel;
    static constexpr std::size_t per_call_working_set = 64 * 1024 * KiB;
    static constexpr std::size_t channel_capacity = 0;
    [[nodiscard]] ::fixy::Option<int> try_pop() noexcept { return ::fixy::Option<int>::some(1); }
};

struct PinProducer {
    using channel_type = PinChannel;
    static constexpr std::size_t per_call_working_set = 64 * 1024 * KiB;
    static constexpr std::size_t channel_capacity = 0;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

// The count of the stage bodies that started, and the mask of the thread of
// each one after its pin.  A stage with a CPU has a mask of one CPU.  A stage
// with no CPU keeps the mask of the caller.
Atomic<std::uint32_t> stage_arrivals{0};
::fixy::futex::FutexWord stage_gate{0};
FixedArray<AffinityMask, kStagesOfTwoRuns> stage_masks{};

// Each body waits until each stage of the two runs started, so the claims of
// the two runs live at the same time.  Then it reads the mask of its thread.
// The last stage opens the gate and wakes the others.  The others sleep on
// the futex word of the gate and do not spin, because many threads that spin
// on one word can starve the threads that still have to arrive.
void pinned_stage(PinConsumer&&, PinProducer&&) noexcept {
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Block>> waiter{eff::testing::bg()};
    std::uint32_t const arrival = stage_arrivals.fetch_add_acq_rel(1);
    if (arrival + 1 == kStagesOfTwoRuns) {
        stage_gate.store_release(1);
        static_cast<void>(::fixy::futex::wake_all(waiter, stage_gate).expect("the kernel wakes the waiting stages"));
    }
    while (stage_gate.load_acquire() == 0) {
        static_cast<void>(::fixy::futex::wait(waiter, stage_gate, 0).expect("a stage waits on the word of the gate"));
    }
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    AffinityMask const mask = ::fixy::sched::read_affinity(bg).expect("a thread reads its own affinity mask");
    // Each stage has its own arrival, so no other thread writes this element at the same time.
    stage_masks.set(Index<kStagesOfTwoRuns>::check(arrival).expect("each stage arrives one time"), mask);
}

// One run of twelve pinned stages.
struct PipelineRun {
    void operator()() noexcept {
        const ::fixy::BgDrainCtx bg{eff::testing::bg()};
        auto const stage = [&bg]() noexcept { return cc::mint_stage<&pinned_stage>(bg, PinConsumer{}, PinProducer{}); };
        auto pipeline = cc::mint_pipeline(bg, stage(), stage(), stage(), stage(), stage(), stage(), stage(), stage(),
                                          stage(), stage(), stage(), stage());
        std::move(pipeline).run(bg);
    }
};

// Two runs of twelve stages at the same time.  The cache clusters of a host
// can hold fewer than twenty-four CPUs each, so a plan that reads only one
// cluster pins two stages to one CPU.  No two pinned stages share a CPU, and
// each CPU is free again after the two runs.
// A stage with no CPU keeps the mask of the caller, so a mask of one CPU
// cannot tell a pinned stage from an unpinned one.  The pair check then does
// not run.
[[nodiscard]] int two_threaded_runs_share_no_pinned_cpu() noexcept {
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    AffinityMask const caller = ::fixy::sched::read_affinity(bg).expect("the test reads its own affinity mask");
    PipelineRun first{};
    PipelineRun second{};
    ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second))
        .expect("the test starts the two threads that run the two pipelines");

    int failures = 0;
    std::size_t pinned = 0;
    bool const can_tell_pins_apart = caller.popcount() >= 2;
    for (Index<kStagesOfTwoRuns> const left : FixedArray<AffinityMask, kStagesOfTwoRuns>::indices()) {
        AffinityMask const& left_mask = stage_masks.at(left);
        if (!can_tell_pins_apart || left_mask.popcount() != 1) continue;
        ++pinned;
        for (Index<kStagesOfTwoRuns> const right : FixedArray<AffinityMask, kStagesOfTwoRuns>::indices()) {
            if (right.raw() <= left.raw() || !(stage_masks.at(right) == left_mask)) continue;
            ::fixy::report(::fixy::Sink::Err, "two threaded runs at the same time pinned stages {} and {} to one CPU\n",
                           left.raw(), right.raw());
            ++failures;
        }
    }
    if (caller.popcount() >= kStagesOfTwoRuns && pinned != kStagesOfTwoRuns) {
        ::fixy::report(::fixy::Sink::Err, "the mask has {} CPUs, but only {} of {} stages got a CPU\n",
                       caller.popcount(), pinned, kStagesOfTwoRuns);
        ++failures;
    }
    for (std::uint16_t cpu = 0; cpu <= AffinityMask::kMaxCore; ++cpu) {
        if (!cc::process_stage_cpu_claims()->is_claimed(cpu)) continue;
        ::fixy::report(::fixy::Sink::Err, "CPU {} stays claimed after the two runs\n", cpu);
        ++failures;
        break;
    }
    return failures == 0 ? 0 : 1;
}

// The count of the CPUs that the stages of an unpinnable run saw claimed.
Atomic<std::uint32_t> claims_seen_by_unpinned_stages{0};

// A stage under the foreground context cannot pin its thread.  It counts the
// claimed CPUs of the process while its run lives.
void unpinned_stage(PinConsumer&&, PinProducer&&) noexcept {
    for (std::uint16_t cpu = 0; cpu <= AffinityMask::kMaxCore; ++cpu) {
        if (cc::process_stage_cpu_claims()->is_claimed(cpu)) {
            static_cast<void>(claims_seen_by_unpinned_stages.fetch_add_acq_rel(1));
        }
    }
}

// A threaded run of stages that cannot pin holds no claim, so a run of
// pinned stages at the same time can take each CPU.
[[nodiscard]] int unpinnable_stages_hold_no_claim() noexcept {
    const ::fixy::HotFgCtx ctx = eff::testing::foreground();
    const ::fixy::BgDrainCtx coordinator{eff::testing::bg()};
    auto pipeline = cc::mint_pipeline(coordinator, cc::mint_stage<&unpinned_stage>(ctx, PinConsumer{}, PinProducer{}),
                                      cc::mint_stage<&unpinned_stage>(ctx, PinConsumer{}, PinProducer{}));
    std::move(pipeline).run(coordinator);
    if (std::uint32_t const seen = claims_seen_by_unpinned_stages.load_acquire(); seen != 0) {
        ::fixy::report(::fixy::Sink::Err, "the stages of a run that cannot pin saw {} claimed CPUs\n", seen);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += six_stages_take_the_next_cluster();
    failures += stages_past_the_mask_run_unpinned();
    failures += two_live_runs_share_no_cpu();
    failures += the_mask_orders_the_clusters();
    failures += two_threaded_runs_share_no_pinned_cpu();
    failures += unpinnable_stages_hold_no_claim();
    return failures == 0 ? 0 : 1;
}
