// The sweep of the region store with a foreground: the marks call of the owner
// and the holders that the foreground writes.

#include "background_thread_run_in_row.h"
#include "trace_rig.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CKernel.h>
#include <crucible/HeldRegions.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/SchemaTable.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
using crucible::BuiltRegion;
using crucible::ConstRegionLink;
using crucible::MetaIndex;

namespace sweep_rig {
namespace {

using Count = ::fixy::Quantity<::fixy::unit::Element, uint32_t>;

// The owner of a pipeline and its foreground, in one object.  The foreground
// writes its holders in cells, as the foreground of a Vigil does, and the marks
// call reads them on the publish stage.  The slot is the region that the owner
// can still give to its foreground.
class Foreground final {
public:
    using Holders = crucible::HeldRegions<Foreground>;
    using Marks = BackgroundThread::RegionMarks;

    // The callback of a published region: it keeps the link of the region.
    void on_ready(::foundation::effects::Bg const&, BackgroundThread::PublishStage const&,
                  crucible::PublishedRegion const& published) noexcept {
        newest_ = published.region;
    }

    // The marks call.
    [[nodiscard]] Marks read_marks(BackgroundThread::PublishStage const&) noexcept {
        Marks marks{};
        marks.handed = slot_.load_acquire();
        held_.try_read().match(
            [&marks](Holders::Read read) noexcept {
                for (const ConstRegionLink& region : read.regions)
                    marks.hold(region);
                marks.foreground = Marks::Foreground::Read;
                marks.foreground_generation = read.generation;
            },
            [&marks] noexcept { marks.foreground = Marks::Foreground::Writing; });
        return marks;
    }

    // Opens a write of the holders for the time of the body.
    template <class Body>
    void write(Body&& body) noexcept {
        held_.with_write_(std::forward<Body>(body));
    }

    // Writes one region into the first cell, and ends the write.
    void hold(ConstRegionLink region) noexcept {
        write([region](auto store_cell) noexcept { store_cell(::fixy::index<0>(), region); });
    }

    // Puts a region into the slot, or takes the region out of it.
    void hand(ConstRegionLink region) noexcept { slot_.store_release(region); }

    // The link of the region of the last publish, which a job of the test gave.
    [[nodiscard]] ConstRegionLink newest() const noexcept { return newest_; }

private:
    Holders held_{};
    ::fixy::Atomic<ConstRegionLink> slot_{};
    ConstRegionLink newest_{};
};

// Builds a region of one op with no tensor and two scalar arguments, so each
// value gives a region of its own content.
template <class Stopped>
[[nodiscard]] BuiltRegion build_region(Stopped& stopped, crucible::MetaLog const& log, int64_t value) {
    crucible::NumericsHash const numerics = stopped.numerics();
    const auto build_in_frame = [&log, numerics, value](BackgroundThread::TraceBuildState& state) noexcept {
        ::fixy::FixedArray<crucible::TraceRing::Entry, 1> trace{};
        trace.update(::fixy::index<0>(), [value](crucible::TraceRing::Entry& entry) noexcept {
            entry.schema_hash = crucible::SchemaHash{0x7000ULL};
            entry.shape_hash = crucible::ShapeHash{0x8000ULL};
            entry.num_scalar_args = 2;
            entry.scalar_values.set(::fixy::index<0>(), value);
            entry.scalar_values.set(::fixy::index<1>(), int64_t{7});
        });
        const ::fixy::FixedArray<MetaIndex, 1> starts{MetaIndex::none()};
        const ::fixy::FixedArray<crucible::ScopeHash, 1> scopes{};
        const ::fixy::FixedArray<crucible::CallsiteHash, 1> sites{};
        const ::fixy::FixedArray<crucible::OpDepth, 1> depths{};
        crucible::TraceScratch scratch{::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 16}};
        ::fixy::Option<BuiltRegion> built{};
        scratch.arena.with_epoch(
            [&state, &log, &built, &trace, &starts, &scopes, &sites, &depths, numerics,
             alloc = ::foundation::effects::testing::test().alloc](crucible::TraceScratch::Epoch& epoch) noexcept {
                built = ::fixy::Option<BuiltRegion>::some(
                    BackgroundThread::build_trace_from(state, log, alloc, epoch, trace.view(), starts.view(),
                                                       scopes.view(), sites.view(), depths.view(), numerics)
                        .expect("the work builds a graph")
                        .region);
            });
        return std::move(built).expect("the epoch built the region");
    };
    return stopped.with_trace_build_state(build_in_frame);
}

// Publishes the region in a job, through either handle of the thread.
template <class Handle>
void publish(Handle& handle, BuiltRegion region) noexcept {
    handle.run_on_publish_stage([&region](BackgroundThread::PublishStageView& owner,
                                          BackgroundThread::PublishStage const& stage) noexcept {
        owner.publish_region(::foundation::effects::testing::bg(), stage, std::move(region), crucible::StreamPoint{});
    });
}

// The count of regions in the store, and the count of regions that a full store
// discarded.
struct StoreCounts final {
    uint32_t stored = 0;
    uint64_t dropped = 0;
    [[nodiscard]] friend bool operator==(StoreCounts const&, StoreCounts const&) noexcept = default;
};

template <class Handle>
[[nodiscard]] StoreCounts read_counts(Handle& handle) noexcept {
    StoreCounts counts{};
    handle.run_on_publish_stage(
        [&counts](BackgroundThread::PublishStageView& owner, BackgroundThread::PublishStage const& stage) noexcept {
            counts = StoreCounts{.stored = owner.region_count(stage), .dropped = owner.regions_dropped_full(stage)};
        });
    return counts;
}

// The regions of a test, which a Stopped handle builds before the pipeline
// starts.  Each one leaves the array one time.
constexpr uint32_t BUILT = 8;
class BuiltRegions final {
public:
    template <class Stopped>
    BuiltRegions(Stopped& stopped, crucible::MetaLog const& log) {
        for (const ::fixy::Index<BUILT> position : built_.indices())
            static_cast<void>(
                built_.exchange(position, build_region(stopped, log, static_cast<int64_t>(position.raw()))));
    }
    [[nodiscard]] BuiltRegion take(uint32_t position) noexcept {
        return built_.exchange(::fixy::Index<BUILT>::check(position).expect("the test built this region"),
                               BuiltRegion{});
    }

private:
    ::fixy::FixedArray<BuiltRegion, BUILT> built_{};
};

// Makes a thread that runs no pipeline, with a foreground and the limits, and
// calls the check with the log, the foreground and the Stopped handle.
template <class Check>
void with_foreground(crucible::PipelineLimits const& limits, Check check) {
    crucible::global_schema_table().seal(kTest);
    crucible::global_ckernel_table().value()->seal(kTest);
    crucible::test::RigLog metalog{};
    crucible::test::RigRing ring{};
    Foreground foreground{};
    BackgroundThread::RegionReadyCall const ready =
        BackgroundThread::RegionReadyCall::bind<&Foreground::on_ready>(foreground);
    BackgroundThread::RegionMarksCall const marks =
        BackgroundThread::RegionMarksCall::bind<&Foreground::read_marks>(foreground);
    crucible::with_background_thread(
        ::foundation::effects::testing::test().alloc, ring.side(), metalog.side(),
        {.on_region_ready = ::fixy::some(::fixy::erase_brand(::fixy::to(ready))),
         .region_marks = ::fixy::some(::fixy::erase_brand(::fixy::to(marks))),
         .limits = limits},
        [&metalog, &foreground, &check](BackgroundThread const& /*bt*/, auto stopped) noexcept {
            check(*metalog.view(), foreground, std::move(stopped));
        });
}

// A store of four regions whose queue keeps only the newest one, so each other
// region stays only for a holder or for a write of the foreground.
[[nodiscard]] crucible::PipelineLimits small_limits() noexcept {
    crucible::PipelineLimits limits{};
    limits.region_store = Count{4};
    limits.uncompiled_queue = Count{1};
    return limits;
}

}  // namespace
}  // namespace sweep_rig

namespace test_background_thread_run_in_row {

// A foreground that stops inside its write of the holders.  The publish stage
// waits for nothing: each publish inside the write returns, and its sweep marks
// no region left and releases none.  So the store becomes full, and the next
// publish discards its region and counts it.  The first publish after the write
// frees a slot with the marks of the finished write.
template <class Handle>
static void check_open_write(Handle& handle, sweep_rig::Foreground& foreground,
                             sweep_rig::BuiltRegions& built) noexcept {
    using namespace sweep_rig;
    publish(handle, built.take(0));
    // Region 0 leaves the view at generation 0.
    publish(handle, built.take(1));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 2, .dropped = 0}));
    // The write of generation 1 names no region, so the marks of it let region 0
    // go.  The open write of generation 2 holds it.
    foreground.hold(::fixy::none);
    foreground.write([&handle, &built](auto store_cell) noexcept {
        publish(handle, built.take(2));
        CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 3, .dropped = 0}));
        store_cell(::fixy::index<0>(), ::fixy::none);
        publish(handle, built.take(3));
        CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 4, .dropped = 0}));
        publish(handle, built.take(4));
        CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 4, .dropped = 1}));
    });
    // The sweep before the publish releases region 0, and marks regions 1 and 2
    // left at generation 2.
    publish(handle, built.take(5));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 4, .dropped = 1}));
    // The next write lets each marked region go.
    foreground.hold(::fixy::none);
    publish(handle, built.take(6));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 2, .dropped = 1}));
}

void test_sweep_waits_for_no_write() {
    using namespace sweep_rig;
    with_foreground(small_limits(), [](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
        BuiltRegions built{stopped, log};
        check_open_write(stopped, foreground, built);
    });
    // The same with the publish stage on its own thread, beside the thread of
    // the test, which is the foreground.
    with_foreground(small_limits(), [](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
        BuiltRegions built{stopped, log};
        auto running = std::move(stopped).start(::fixy::InitLoadCtx{::foundation::effects::testing::init()});
        check_open_write(running, foreground, built);
        static_cast<void>(std::move(running).stop());
    });
    ::fixy::check::pass(kTest, "  sweep_waits_for_no_write:                  PASSED\n");
}

// A region that the owner hands again, while the foreground writes its holders.
// The region was marked left before, and the sweep inside the write takes that
// mark away, because the foreground can read the region again.  The foreground
// takes the region, and its next write names the region.  Between the two, no
// marks name the region, and the store keeps it only for its missing mark.
template <class Handle>
static void check_region_handed_again(Handle& handle, sweep_rig::Foreground& foreground,
                                      sweep_rig::BuiltRegions& built) noexcept {
    using namespace sweep_rig;
    publish(handle, built.take(0));
    const ConstRegionLink handed = foreground.newest();
    // Region 0 leaves the view at generation 0.
    publish(handle, built.take(1));
    foreground.hold(::fixy::none);
    foreground.write([&handle, &foreground, &built, handed](auto) noexcept {
        foreground.hand(handed);
        publish(handle, built.take(2));
        foreground.hand(::fixy::none);
    });
    // The foreground has region 0 and did not write it.  The sweep marks it left
    // at generation 2 and releases nothing.
    publish(handle, built.take(3));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 4, .dropped = 0})
                                    && "the sweep released a region that the foreground took");
    // The write of generation 3 names region 0.  Regions 1 and 2 go.
    foreground.hold(handed);
    publish(handle, built.take(4));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 3, .dropped = 0}));
    // The foreground drops region 0 in the write of generation 4, and it goes
    // with region 3, which left the view at generation 3.
    foreground.hold(::fixy::none);
    publish(handle, built.take(5));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 2, .dropped = 0}));
}

void test_region_handed_again_keeps_no_old_mark() {
    using namespace sweep_rig;
    with_foreground(small_limits(), [](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
        BuiltRegions built{stopped, log};
        check_region_handed_again(stopped, foreground, built);
    });
    ::fixy::check::pass(kTest, "  region_handed_again_keeps_no_old_mark:     PASSED\n");
}

// A region that the owner gives to its foreground outside the slot, as a
// rollback does.  The job of the owner takes the old mark of the region away
// (keep_region), so the store keeps the region until a later write of the
// holders that does not name it.
template <class Handle>
static void check_kept_region(Handle& handle, sweep_rig::Foreground& foreground,
                              sweep_rig::BuiltRegions& built) noexcept {
    using namespace sweep_rig;
    publish(handle, built.take(0));
    const ConstRegionLink kept = foreground.newest();
    // Region 0 leaves the view at generation 0, and the write of generation 1
    // lets it go.
    publish(handle, built.take(1));
    foreground.hold(::fixy::none);
    handle.run_on_publish_stage(
        [kept](BackgroundThread::PublishStageView& owner, BackgroundThread::PublishStage const& stage) noexcept {
            owner.keep_region(stage, kept);
        });
    publish(handle, built.take(2));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 3, .dropped = 0})
                                    && "the sweep released a region that the owner kept");
    // The write of generation 2 names region 0, and region 1 goes.
    foreground.hold(kept);
    publish(handle, built.take(3));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 3, .dropped = 0}));
    // The write of generation 3 names no region, so regions 0 and 2 go.
    foreground.hold(::fixy::none);
    publish(handle, built.take(4));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 2, .dropped = 0}));
}

void test_kept_region_keeps_no_old_mark() {
    using namespace sweep_rig;
    with_foreground(small_limits(), [](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
        BuiltRegions built{stopped, log};
        check_kept_region(stopped, foreground, built);
    });
    ::fixy::check::pass(kTest, "  kept_region_keeps_no_old_mark:             PASSED\n");
}

}  // namespace test_background_thread_run_in_row
