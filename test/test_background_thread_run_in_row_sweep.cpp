// The sweep of the region store with a foreground: the marks call of the owner
// and the holders that the foreground writes.  The last test compares the
// sweep with a walk of each region in random runs.

#include "background_thread_run_in_row.h"
#include "foundation/philox_stream.h"
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

using RecordLink = ::fixy::Option<::fixy::Ref<crucible::PublishedRegion const>>;

// What an owner of a test has: a foreground, or none.
enum class OwnerKind : uint8_t {
    WithForeground,
    WithNoForeground,
};

// What the callback of an owner does with each published region, beside the
// links that it keeps: it puts the region into the slot, as a Vigil does, or it
// leaves the slot as it is.
enum class OnPublish : uint8_t {
    LeavesTheSlot,
    FillsTheSlot,
};

// The owner of a pipeline and its foreground, in one object.  The foreground
// writes its holders in cells, as the foreground of a Vigil does, and the marks
// call reads them on the publish stage.  The slot is the region that the owner
// can still give to its foreground.  The log holds regions beside the
// foreground, as the transaction log of a Vigil does.
class Foreground final {
public:
    using Holders = crucible::HeldRegions<Foreground>;
    using Marks = BackgroundThread::RegionMarks;
    static constexpr uint32_t LOG_REGIONS = 3;

    explicit Foreground(OwnerKind kind = OwnerKind::WithForeground,
                        OnPublish on_publish = OnPublish::LeavesTheSlot) noexcept
        : kind_{kind}, on_publish_{on_publish} {}

    // The callback of a published region: it keeps the links of the region.
    void on_ready(::foundation::effects::Bg const&, BackgroundThread::PublishStage const&,
                  crucible::PublishedRegion const& published) noexcept {
        newest_ = published.region;
        newest_record_ = ::fixy::some(::fixy::erase_brand(::fixy::to(published)));
        if (on_publish_ == OnPublish::FillsTheSlot) hand(newest_);
    }

    // The marks call.
    [[nodiscard]] Marks read_marks(BackgroundThread::PublishStage const&) noexcept { return marks(); }

    // The marks at this time.
    [[nodiscard]] Marks marks() const noexcept {
        Marks marks{};
        marks.handed = slot_.load_acquire();
        for (const ConstRegionLink& region : log_)
            marks.hold(region);
        if (kind_ == OwnerKind::WithNoForeground) return marks;
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

    // Puts a region into one place of the log, or takes the region out of it.
    void log(::fixy::Index<LOG_REGIONS> position, ConstRegionLink region) noexcept { log_.set(position, region); }

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

    // The links of the region of the last publish, which a job of the test gave,
    // and of its record in the region store.  The store keeps the slot of the
    // record, and a release of the region empties the record.
    [[nodiscard]] ConstRegionLink newest() const noexcept { return newest_; }
    [[nodiscard]] RecordLink newest_record() const noexcept { return newest_record_; }

private:
    OwnerKind kind_;
    OnPublish on_publish_;
    Holders held_{};
    ::fixy::Atomic<ConstRegionLink> slot_{};
    ::fixy::FixedArray<ConstRegionLink, LOG_REGIONS> log_{};
    ConstRegionLink newest_{};
    RecordLink newest_record_{};
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
void publish(Handle& handle, BuiltRegion region, crucible::StreamPoint point = crucible::StreamPoint{}) noexcept {
    handle.run_on_publish_stage([&region, point](BackgroundThread::PublishStageView& owner,
                                                 BackgroundThread::PublishStage const& stage) noexcept {
        owner.publish_region(::foundation::effects::testing::bg(), stage, std::move(region), point);
    });
}

// Marks the region as one that the owner gave to its foreground (keep_region).
template <class Handle>
void keep(Handle& handle, ConstRegionLink region) noexcept {
    handle.run_on_publish_stage(
        [region](BackgroundThread::PublishStageView& owner, BackgroundThread::PublishStage const& stage) noexcept {
            owner.keep_region(stage, region);
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

// Makes a thread that runs no pipeline, with the foreground and the limits, and
// calls the check with the log, the foreground and the Stopped handle.
template <class Check>
void with_foreground(crucible::test::RigLog& metalog, Foreground& foreground, crucible::PipelineLimits const& limits,
                     Check check) {
    crucible::global_schema_table().seal(kTest);
    crucible::global_ckernel_table().value()->seal(kTest);
    crucible::test::RigRing ring{};
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

// The same with a new log, a new foreground, and a store of four regions whose
// queue keeps only the newest one.  So each other region stays only for a
// holder or for a write of the foreground.
template <class Check>
void with_small_store(Check check) {
    crucible::PipelineLimits limits{};
    limits.region_store = Count{4};
    limits.uncompiled_queue = Count{1};
    crucible::test::RigLog metalog{};
    Foreground foreground{};
    with_foreground(metalog, foreground, limits, std::move(check));
}

// The count of publishes of one random run.
constexpr uint32_t PUBLISHES = 200;

// One region of a random run: the region and its record, and what the walk of
// each region knows of it.
struct WalkedRegion final {
    ConstRegionLink region{};
    RecordLink record{};
    // The reset point of the record.  Each publish of a run gives another one,
    // so it tells whether the slot still holds this region.
    crucible::StreamPoint point{};
    crucible::ContentHash hash{};
    uint64_t pushes_before = 0;
    uint64_t left_generation = crucible::RegionMemory::NOT_LEFT;
    bool is_live = false;
};

// What the runs of a test did, so the test can say that each case occurred.
struct RunCounts final {
    uint64_t released = 0;
    uint64_t dropped = 0;
    uint64_t swept_full = 0;
    uint64_t published_in_a_write = 0;
};

// The oracle of the sweep: the rule of a release, with a walk of each region of
// the store after each publish, and one more walk before a publish into a full
// store.  It knows no set of slots.
class WalkOracle final {
public:
    using Marks = BackgroundThread::RegionMarks;

    explicit WalkOracle(crucible::PipelineLimits const& limits) noexcept
        : store_{limits.region_store.raw()}, queue_{limits.uncompiled_queue.raw()} {}

    // True when the store takes one region more.  A full store gets a walk with
    // the marks first, and a store that stays full drops the region.
    [[nodiscard]] bool admits(Marks const& marks, RunCounts& counts) noexcept {
        if (live_ == store_) {
            ++counts.swept_full;
            walk(marks, counts);
        }
        if (live_ < store_) return true;
        ++dropped_;
        ++counts.dropped;
        return false;
    }

    // Adds the region of a publish.  The walk of that publish follows.
    void add(WalkedRegion region) noexcept {
        region.pushes_before = pushes_;
        region.is_live = true;
        regions_.set(::fixy::Index<PUBLISHES>::check(count_).expect("a run has PUBLISHES publishes"), region);
        ++pushes_;
        ++count_;
        ++live_;
        last_ = region.region;
    }

    // Walks each region of the store with the marks.
    void walk(Marks const& marks, RunCounts& counts) noexcept {
        for (const ::fixy::Index<PUBLISHES> position : regions_.indices()) {
            WalkedRegion walked = regions_.at(position);
            if (!walked.is_live) continue;
            if (::fixy::same_target(walked.region, last_) || ::fixy::same_target(walked.region, marks.handed)) {
                walked.left_generation = crucible::RegionMemory::NOT_LEFT;
            } else if (marks.foreground == Marks::Foreground::Writing) {
                continue;
            } else if (marks.foreground == Marks::Foreground::Read
                       && walked.left_generation == crucible::RegionMemory::NOT_LEFT) {
                walked.left_generation = marks.foreground_generation;
            } else if (!holds(marks, walked)
                       && !(marks.foreground == Marks::Foreground::Read
                            && marks.foreground_generation <= walked.left_generation)) {
                walked.is_live = false;
                --live_;
                ++counts.released;
            }
            regions_.set(position, walked);
        }
    }

    // The mark of keep_region.
    void keep(ConstRegionLink region) noexcept {
        for (const ::fixy::Index<PUBLISHES> position : regions_.indices()) {
            WalkedRegion walked = regions_.at(position);
            if (!walked.is_live || !::fixy::same_target(walked.region, region)) continue;
            walked.left_generation = crucible::RegionMemory::NOT_LEFT;
            regions_.set(position, walked);
        }
    }

    // A random region of the store, or none for an empty store.
    [[nodiscard]] ConstRegionLink draw_live(crucible::test::PhiloxStream& draws) const noexcept {
        if (live_ == 0) return ::fixy::none;
        uint64_t remaining = draws.below(live_);
        for (const WalkedRegion& walked : regions_) {
            if (!walked.is_live) continue;
            if (remaining == 0) return walked.region;
            --remaining;
        }
        return ::fixy::none;
    }

    // Compares the store with the walk, region by region.  A region that the
    // walk keeps must stand in its slot, and its node must be alive, which the
    // address sanitizer checks.  A region that the walk released must not.
    void check(StoreCounts const& counts) const noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, counts.stored == live_ && counts.dropped == dropped_);
        for (const WalkedRegion& walked : regions_) {
            for (const auto& record : walked.record) {
                const bool is_stored = record->region.is_some() && record->reset_point == walked.point;
                CRUCIBLE_TEST_ASSERT(kTest, is_stored == walked.is_live);
            }
            if (!walked.is_live) continue;
            CRUCIBLE_TEST_ASSERT(kTest, ConstRegionLink{walked.region}.expect("a live region has a node")->content_hash
                                            == walked.hash);
        }
    }

private:
    // True when the queue or the marks name the region.
    [[nodiscard]] bool holds(Marks const& marks, WalkedRegion const& walked) const noexcept {
        if (pushes_ - walked.pushes_before - 1 < queue_) return true;
        for (const auto& held : marks.held) {
            if (::fixy::same_target(walked.region, ConstRegionLink::some(held))) return true;
        }
        return false;
    }

    uint64_t store_ = 0;
    uint64_t queue_ = 0;
    uint64_t pushes_ = 0;
    uint64_t count_ = 0;
    uint64_t live_ = 0;
    uint64_t dropped_ = 0;
    ConstRegionLink last_{};
    ::fixy::FixedArray<WalkedRegion, PUBLISHES> regions_{};
};

// Writes a random choice of the regions of the store into the cells.
void write_random_holders(Foreground& foreground, WalkOracle const& oracle, crucible::test::PhiloxStream& draws) {
    foreground.write([&oracle, &draws](auto store_cell) noexcept {
        for (const Foreground::Holders::Position position : ::fixy::IndexRange<Foreground::Holders::CAP>{})
            store_cell(position, draws.below(3) == 0 ? oracle.draw_live(draws) : ConstRegionLink{});
    });
}

// One random act of the owner or of its foreground between two publishes.
template <class Stopped>
void act_at_random(Stopped& stopped, Foreground& foreground, OwnerKind kind, WalkOracle& oracle,
                   crucible::test::PhiloxStream& draws) {
    switch (draws.below(6)) {
        case 0:
            if (kind == OwnerKind::WithForeground) write_random_holders(foreground, oracle, draws);
            break;
        case 1:
            if (kind == OwnerKind::WithForeground) foreground.hold(::fixy::none);
            break;
        case 2:
            foreground.hand(::fixy::none);
            break;
        case 3:
            foreground.hand(oracle.draw_live(draws));
            break;
        case 4: {
            const ConstRegionLink kept = oracle.draw_live(draws);
            keep(stopped, kept);
            oracle.keep(kept);
            break;
        }
        default:
            foreground.log(::fixy::Index<Foreground::LOG_REGIONS>::check(draws.below(Foreground::LOG_REGIONS))
                               .expect("the draw is below the bound"),
                           draws.below(2) == 0 ? oracle.draw_live(draws) : ConstRegionLink{});
            break;
    }
}

// Publishes one region, and compares the store with the walk after it.
template <class Stopped>
void publish_and_compare(Stopped& stopped, crucible::MetaLog const& log, Foreground& foreground, WalkOracle& oracle,
                         uint32_t number, RunCounts& counts) {
    BuiltRegion region = build_region(stopped, log, int64_t{number});
    const crucible::ContentHash hash = region.content_hash();
    const crucible::StreamPoint point = crucible::StreamPoint::mark_at(uint64_t{number} + 1);
    const bool is_admitted = oracle.admits(foreground.marks(), counts);
    publish(stopped, std::move(region), point);
    if (is_admitted) {
        oracle.add(WalkedRegion{
            .region = foreground.newest(), .record = foreground.newest_record(), .point = point, .hash = hash});
        oracle.walk(foreground.marks(), counts);
    }
    oracle.check(read_counts(stopped));
}

// One random run: PUBLISHES publishes, with random acts before each one.  One
// publish of four stands inside an open write of the holders.
template <class Stopped>
void run_at_random(Stopped& stopped, crucible::MetaLog const& log, Foreground& foreground, OwnerKind kind,
                   crucible::PipelineLimits const& limits, uint64_t seed, RunCounts& counts) {
    crucible::test::PhiloxStream draws{seed};
    WalkOracle oracle{limits};
    for (uint32_t number = 0; number < PUBLISHES; ++number) {
        for (uint64_t acts = draws.below(4); acts != 0; --acts)
            act_at_random(stopped, foreground, kind, oracle, draws);
        if (kind == OwnerKind::WithForeground && draws.below(4) == 0) {
            ++counts.published_in_a_write;
            foreground.write([&stopped, &log, &foreground, &oracle, &counts, number](auto store_cell) noexcept {
                store_cell(::fixy::index<0>(), ::fixy::none);
                publish_and_compare(stopped, log, foreground, oracle, number, counts);
                store_cell(::fixy::index<1>(), foreground.newest());
            });
            continue;
        }
        publish_and_compare(stopped, log, foreground, oracle, number, counts);
    }
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
    with_small_store([](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
        BuiltRegions built{stopped, log};
        check_open_write(stopped, foreground, built);
    });
    // The same with the publish stage on its own thread, beside the thread of
    // the test, which is the foreground.
    with_small_store([](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
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

// The same for a region that the queue still holds.  The sweep settled it in the
// queue after its mark, so the set of slots does not name it, and the sweep
// finds it in the store.
template <class Handle>
static void check_queued_region_handed_again(Handle& handle, sweep_rig::Foreground& foreground,
                                             sweep_rig::BuiltRegions& built) noexcept {
    using namespace sweep_rig;
    publish(handle, built.take(0));
    const ConstRegionLink handed = foreground.newest();
    // Region 0 leaves the view at generation 0, and the queue holds it.
    publish(handle, built.take(1));
    foreground.hold(::fixy::none);
    foreground.hand(handed);
    publish(handle, built.take(2));
    foreground.hand(::fixy::none);
    // Region 0 leaves the queue.  The foreground has it and did not write it.
    publish(handle, built.take(3));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 4, .dropped = 0})
                                    && "the sweep released a region that the foreground took");
    // The write of generation 2 names region 0.  Region 1 leaves the queue and
    // goes.
    foreground.hold(handed);
    publish(handle, built.take(4));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 4, .dropped = 0}));
    // The write of generation 3 names no region.  Region 2 leaves the queue and
    // goes with region 0.
    foreground.hold(::fixy::none);
    publish(handle, built.take(5));
    CRUCIBLE_TEST_ASSERT(kTest, read_counts(handle) == StoreCounts({.stored = 3, .dropped = 0}));
}

void test_region_handed_again_keeps_no_old_mark() {
    using namespace sweep_rig;
    with_small_store([](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
        BuiltRegions built{stopped, log};
        check_region_handed_again(stopped, foreground, built);
    });
    crucible::PipelineLimits limits{};
    limits.region_store = Count{8};
    limits.uncompiled_queue = Count{3};
    crucible::test::RigLog metalog{};
    Foreground foreground{};
    with_foreground(metalog, foreground, limits,
                    [](crucible::MetaLog const& log, Foreground& held, auto stopped) noexcept {
                        BuiltRegions built{stopped, log};
                        check_queued_region_handed_again(stopped, held, built);
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
    keep(handle, kept);
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
    with_small_store([](crucible::MetaLog const& log, Foreground& foreground, auto stopped) noexcept {
        BuiltRegions built{stopped, log};
        check_kept_region(stopped, foreground, built);
    });
    ::fixy::check::pass(kTest, "  kept_region_keeps_no_old_mark:             PASSED\n");
}

// The sweep reads only the slots that it can change, and it must do what a walk
// of each region does.  Each run draws its acts from a fixed seed: writes of
// the holders, regions that the owner hands, keeps or logs, and publishes
// inside an open write.  After each publish, the store must hold the regions of
// the walk and no other one.
void test_sweep_matches_a_walk_of_each_region() {
    using namespace sweep_rig;
    struct Sizes final {
        uint32_t store = 0;
        uint32_t queue = 0;
    };
    const ::fixy::FixedArray<Sizes, 6> sizes{Sizes{.store = 4, .queue = 1},  Sizes{.store = 6, .queue = 6},
                                             Sizes{.store = 8, .queue = 0},  Sizes{.store = 8, .queue = 3},
                                             Sizes{.store = 16, .queue = 8}, Sizes{.store = 128, .queue = 64}};
    const ::fixy::FixedArray<OwnerKind, 2> kinds{OwnerKind::WithForeground, OwnerKind::WithNoForeground};
    const ::fixy::FixedArray<OnPublish, 2> callbacks{OnPublish::LeavesTheSlot, OnPublish::FillsTheSlot};
    constexpr uint64_t SEEDS = 2;
    crucible::test::RigLog metalog{};
    RunCounts counts{};
    uint64_t seed = 0x5EED;
    for (const Sizes& size : sizes) {
        crucible::PipelineLimits limits{};
        limits.region_store = Count{size.store};
        limits.uncompiled_queue = Count{size.queue};
        for (const OwnerKind kind : kinds) {
            for (const OnPublish on_publish : callbacks) {
                for (uint64_t run = 0; run < SEEDS; ++run) {
                    Foreground foreground{kind, on_publish};
                    ++seed;
                    with_foreground(metalog, foreground, limits,
                                    [&limits, &counts, kind, seed](crucible::MetaLog const& log, Foreground& held,
                                                                   auto stopped) noexcept {
                                        run_at_random(stopped, log, held, kind, limits, seed, counts);
                                    });
                }
            }
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, counts.released > 1000 && counts.dropped > 100 && counts.swept_full > 100);
    CRUCIBLE_TEST_ASSERT(kTest, counts.published_in_a_write > 500);
    ::fixy::check::pass(kTest,
                        "  sweep_matches_a_walk_of_each_region:       "
                        "PASSED (released={}, dropped={}, swept_full={})\n",
                        counts.released, counts.dropped, counts.swept_full);
}

}  // namespace test_background_thread_run_in_row
