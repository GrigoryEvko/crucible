// The handles of a BackgroundThread (the owner of a thread of
// crucible/BackgroundThread.h).
//
// A Stopped handle opens each door of a thread that runs no pipeline, and a
// start and a stop give the next handle.  A handle that a move took opens no
// door, so each door of it ends the process in each build.  A frame of
// with_trace_build_state is no run of the pipeline, so a flush beside it finds
// no pipeline and does not wait.
//
// A thread that holds the publish stage runs a job or a region callback.  A
// wait of that thread on its own stage has no end, so a flush of a callback ends
// the process.  A job captures no handle and no BackgroundThread, so no job
// flushes and no job posts a job (neg_background_thread_job_in_a_job).
//
// The target test_background_thread_handles_ignore compiles this file with the
// ignore semantic, where a CRUCIBLE_PRE is only an assumption.  Each check must
// hold there too.  Each broken use runs in a child process through expect_dies
// of the Check family, while the test process has one thread.  The children
// share one rig, each in its own copy of the process.

#include <crucible/BackgroundThread.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include "test_harness.h"
#include "trace_rig.h"

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::TraceRing;
using crucible::test::TraceRig;
using BuildState = BackgroundThread::TraceBuildState;

constexpr uint64_t kBuildValue = 40;

constexpr ::fixy::TextView kEmptyStopped{"a Stopped handle that a move took"};
constexpr ::fixy::TextView kEmptyRunning{"a Running handle that a move took"};
constexpr ::fixy::TextView kOwnStage{"fatal invariant failed: !holds_stage_on_this_thread_()"};

[[nodiscard]] ::fixy::InitLoadCtx startup() noexcept {
    return ::fixy::InitLoadCtx{::foundation::effects::testing::init()};
}

[[nodiscard]] ::fixy::BgLoadCtx background() noexcept {
    return ::fixy::BgLoadCtx{::foundation::effects::testing::bg()};
}

// Opens the frame of a thread on the rig with the test of the thread and its
// Stopped handle.
template <class Test>
void with_rig_thread(TraceRig& rig, Test test) {
    crucible::with_background_thread(
        ::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(), {},
        [&test](BackgroundThread const& bt, auto stopped) noexcept { test(bt, std::move(stopped)); });
}

// Each door of a Stopped handle, with the handles of a start and a stop.  A
// frame gives the value of its body.  The stop signal ends the run of
// run_in_row.
template <class Stopped>
void check_each_door(BackgroundThread const& bt, Stopped stopped) noexcept {
    CRUCIBLE_TEST_ASSERT(kTest, stopped.pending_op_count() == 0 && stopped.detector_history_count() == 0);
    const crucible::NumericsHash first = stopped.numerics();
    const crucible::NumericsHash second{first.raw() ^ uint64_t{1}};
    stopped.set_numerics(second);
    CRUCIBLE_TEST_ASSERT(kTest, stopped.numerics() == second);
    const uint64_t given = stopped.with_trace_build_state([](BuildState&) noexcept { return kBuildValue; });
    CRUCIBLE_TEST_ASSERT(kTest, given == kBuildValue);
    static_cast<void>(stopped.build_scratch_retained_bytes());

    auto running = std::move(stopped).start(startup(), BackgroundThread::PlanTarget{.rank = 3, .world_size = 5});
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    auto stopped_again = std::move(running).stop();
    BackgroundThread::PlanTarget const target = stopped_again.plan_target();
    CRUCIBLE_TEST_ASSERT(kTest, target.rank == 3 && target.world_size == 5);

    stopped_again.rearm_stop();
    bt.stop_requested.signal();
    stopped_again.run_in_row(background());
    CRUCIBLE_TEST_ASSERT(kTest, stopped_again.pending_op_count() == 0);
}

// The doors of a handle that a move took.
enum class EmptyDoor : uint8_t {
    start,
    numerics,
    run_on_publish_stage,
    stop,
    run_on_publish_stage_of_running,
};

// Takes the handle of the source, which the caller still names.
template <class Stopped>
[[nodiscard]] auto take_and_start(Stopped& source) noexcept {
    return std::move(source).start(startup());
}
template <class Running>
void take_and_stop(Running& source) noexcept {
    static_cast<void>(std::move(source).stop());
}

// A move empties the source.  Each door of the source below ends the process.
template <class Stopped>
void call_empty_door(Stopped& stopped, EmptyDoor door) noexcept {
    auto running = take_and_start(stopped);
    if (door == EmptyDoor::start) return static_cast<void>(take_and_start(stopped));
    if (door == EmptyDoor::numerics) return static_cast<void>(stopped.numerics());
    if (door == EmptyDoor::run_on_publish_stage) {
        return stopped.run_on_publish_stage(
            [](BackgroundThread::PublishStageView&, BackgroundThread::PublishStage const&) noexcept {});
    }
    take_and_stop(running);
    if (door == EmptyDoor::stop) return take_and_stop(running);
    running.run_on_publish_stage(
        [](BackgroundThread::PublishStageView&, BackgroundThread::PublishStage const&) noexcept {});
}

template <class Stopped>
void check_empty_door_ends_the_process(Stopped& stopped, EmptyDoor door, ::fixy::TextView text) noexcept {
    ::fixy::check::expect_dies(kTest, [&stopped, door] noexcept { call_empty_door(stopped, door); }, text);
}

template <class Stopped>
void check_empty_handles(Stopped stopped) noexcept {
    check_empty_door_ends_the_process(stopped, EmptyDoor::start, kEmptyStopped);
    check_empty_door_ends_the_process(stopped, EmptyDoor::numerics, kEmptyStopped);
    check_empty_door_ends_the_process(stopped, EmptyDoor::run_on_publish_stage, kEmptyStopped);
    check_empty_door_ends_the_process(stopped, EmptyDoor::stop, kEmptyRunning);
    check_empty_door_ends_the_process(stopped, EmptyDoor::run_on_publish_stage_of_running, kEmptyRunning);
}

// A frame is no run of the pipeline, so a flush of a second thread beside it
// does not wait for its end.  The ring holds an op that no pipeline reads.
template <class Stopped>
void check_flush_beside_a_frame(TraceRig& rig, BackgroundThread const& bt, Stopped stopped) noexcept {
    CRUCIBLE_TEST_ASSERT(kTest, rig.append_entry(TraceRing::Entry{}, MetaIndex::none()));
    ::fixy::Atomic<bool> is_held{false};
    ::fixy::Atomic<bool> has_returned{false};
    auto flush = [&bt, &is_held, &has_returned]() noexcept {
        while (!is_held.load_acquire()) {
            CRUCIBLE_SPIN_PAUSE;
        }
        CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_err_and([](crucible::FlushRefusal why) noexcept {
            return why == crucible::FlushRefusal::pipeline_stopped;
        }));
        has_returned.store_release(true);
    };
    auto hold = [&stopped, &is_held, &has_returned]() noexcept {
        stopped.with_trace_build_state([&is_held, &has_returned](BuildState&) noexcept {
            is_held.store_release(true);
            while (!has_returned.load_acquire()) {
                CRUCIBLE_SPIN_PAUSE;
            }
        });
    };
    ::fixy::BgLoadCtx const bg = background();
    CRUCIBLE_TEST_ASSERT(
        kTest,
        ::fixy::thread::run_beside(bg, ::fixy::thread::ThreadOptions{}, std::move(flush), std::move(hold)).is_ok());
}

// A region callback that flushes its thread.  The frame gives it the thread
// before the start of the pipeline, and the publish stage calls it.
struct FlushingCallback final {
    ::fixy::Option<::fixy::Ref<BackgroundThread const>> thread{};

    void on_region_ready(::foundation::effects::Bg const& /*bg*/, BackgroundThread::PublishStage const& /*stage*/,
                         crucible::PublishedRegion const& /*published*/) noexcept {
        for (::fixy::Ref<BackgroundThread const> const shared : thread)
            static_cast<void>(shared->flush(kTest));
    }
};

// A flush of a region callback waits on the publish stage, which runs the
// callback, so it ends the process.  A callback has no handle, but it can have
// the shared thread.  The child records a loop until the pipeline processed each
// op, so the pipeline publishes a region and calls the callback.
template <class Stopped>
void run_a_loop(TraceRig& rig, BackgroundThread const& bt, Stopped stopped) noexcept {
    constexpr uint64_t kPeriod = 8;
    constexpr uint64_t kIterations = 8;
    auto running = std::move(stopped).start(startup());
    for (uint64_t index = 0; index < kPeriod * kIterations; ++index) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{0x300 + index % kPeriod};
        entry.shape_hash = crucible::ShapeHash{0x400 + index % kPeriod};
        CRUCIBLE_TEST_ASSERT(kTest, rig.append_entry(entry, MetaIndex::none()));
    }
    while (bt.total_processed.load_acquire() < rig.ring_total_produced()) {
        CRUCIBLE_SPIN_PAUSE;
    }
    static_cast<void>(std::move(running).stop());
}

void test_flush_of_a_callback_ends_the_process(TraceRig& rig) {
    FlushingCallback callback{};
    BackgroundThread::RegionReadyCall const on_ready =
        BackgroundThread::RegionReadyCall::bind<&FlushingCallback::on_region_ready>(callback);
    ::fixy::check::expect_dies(
        kTest,
        [&rig, &callback, &on_ready] noexcept {
            crucible::with_background_thread(
                ::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side(),
                {.on_region_ready = ::fixy::some(::fixy::erase_brand(::fixy::to(on_ready)))},
                [&rig, &callback](BackgroundThread const& bt, auto stopped) noexcept {
                    callback.thread = ::fixy::some(::fixy::erase_brand(::fixy::to(bt)));
                    run_a_loop(rig, bt, std::move(stopped));
                });
        },
        kOwnStage);
}

}  // namespace

int main() {
    TraceRig rig{};
    with_rig_thread(
        rig, [](BackgroundThread const& /*bt*/, auto stopped) noexcept { check_empty_handles(std::move(stopped)); });
    test_flush_of_a_callback_ends_the_process(rig);
    with_rig_thread(rig,
                    [](BackgroundThread const& bt, auto stopped) noexcept { check_each_door(bt, std::move(stopped)); });
    with_rig_thread(rig, [&rig](BackgroundThread const& bt, auto stopped) noexcept {
        check_flush_beside_a_frame(rig, bt, std::move(stopped));
    });
    ::fixy::check::pass(kTest, "test_background_thread_handles: all passed\n");
    return 0;
}
