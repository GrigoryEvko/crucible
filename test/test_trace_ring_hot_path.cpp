// The tier of the hot path is a view of the effect row of a context, and
// no value stores it.  The pure variants of the append and drain calls of
// the ring and the metadata log take a context whose row is pure, so the
// view gives each such context the tier Hot.  The pure variants refuse a
// context whose row holds Alloc.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "trace_neg/ring_ends.h"
#include "trace_rig.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::CallsiteHash;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::ScopeHash;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::TensorMeta;
using crucible::TraceRing;
using ::fixy::hot_path_tier_v;
using ::fixy::HotPathTier_v;

namespace eff = ::foundation::effects;

namespace {

using ForegroundCtx = decltype(eff::testing::foreground());
using AllocCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc>>;

// An entry carries no operation index of its own: its slot in the ring is
// that index.  Only the two hashes need seeding here.
TraceRing::Entry make_entry(std::uint64_t schema_seed) noexcept {
    TraceRing::Entry entry{};
    entry.schema_hash = SchemaHash{schema_seed};
    entry.shape_hash = ShapeHash{schema_seed * 31};
    return entry;
}

template <class Ctx>
concept AppendsToRing =
    requires(TraceRing::Producer& ring, Ctx const& ctx) { TraceRing::try_append_pure(ctx, ring, make_entry(0)); };

// The brand of the run of the check below.  The check names only the type
// of a run, so no run of this brand exists.
struct DrainRunBrand final {};

template <class Ctx>
concept DrainsRing =
    requires(TraceRing::Consumer& ring, Ctx const& ctx, ::fixy::ViewMut<TraceRing::Entry, DrainRunBrand> out) {
        TraceRing::drain_pure(ctx, ring, std::move(out));
    };

template <class Ctx>
concept AppendsToLog = requires(MetaLog::Producer& log, Ctx const& ctx, crucible::MetaRun run) {
    MetaLog::try_append_pure(ctx, log, run);
};

// The foreground row is hot, and each pure variant takes it.  The row of
// the test context holds Test and Alloc, so it is cold, and each pure
// variant refuses it.  The row of the observe context of a background
// thread holds Bg and Nondet, so it is cold too.
static_assert(hot_path_tier_v<eff::row_type_of_t<ForegroundCtx>> == HotPathTier_v::Hot);
static_assert(hot_path_tier_v<eff::row_type_of_t<AllocCtx>> == HotPathTier_v::Cold);
static_assert(hot_path_tier_v<eff::row_type_of_t<::fixy::BgObserveCtx>> == HotPathTier_v::Cold);
static_assert(AppendsToRing<ForegroundCtx> && DrainsRing<ForegroundCtx> && AppendsToLog<ForegroundCtx>);
static_assert(!AppendsToRing<AllocCtx> && !DrainsRing<AllocCtx> && !AppendsToLog<AllocCtx>);

// A hot row meets a warm or a cold budget, and a warm row does not meet a
// hot budget.
static_assert(::fixy::HotPathLattice::leq(HotPathTier_v::Warm, hot_path_tier_v<eff::Row<>>));
static_assert(!::fixy::HotPathLattice::leq(HotPathTier_v::Hot, hot_path_tier_v<eff::Row<eff::Effect::Alloc>>));

// The ring and the log are on the heap, and each test writes them in the
// frame with_mut(std::move(boxed), body) of their Box.
void test_ring_append_under_the_foreground() {
    ::fixy::Box<crucible::test::OpenRing> boxed =
        ::fixy::mint_box<crucible::test::OpenRing>(eff::testing::test().alloc);
    boxed = ::fixy::with_mut(std::move(boxed), [](crucible::test::OpenRing& ring) noexcept {
        auto const foreground = eff::testing::foreground();
        CRUCIBLE_TEST_ASSERT(
            kTest, ring.try_append_pure(foreground, make_entry(1), MetaIndex::none(), ScopeHash{0}, CallsiteHash{0}));
        auto [drained, drained_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<TraceRing::Entry, 4>);
        CRUCIBLE_TEST_ASSERT(kTest, drained.with_mut(drained_key, [&ring, &foreground](auto run) noexcept {
            return ring.drain_pure(foreground, std::move(run));
        }) == 1u);
        CRUCIBLE_TEST_ASSERT(kTest, drained.with_mut(drained_key, [&ring, &foreground](auto run) noexcept {
            return ring.drain_pure(foreground, std::move(run));
        }) == 0u);
    });
}

void test_log_append_under_the_foreground() {
    ::fixy::Box<crucible::test::OpenLog> boxed = ::fixy::mint_box<crucible::test::OpenLog>(eff::testing::test().alloc);
    boxed = ::fixy::with_mut(std::move(boxed), [](crucible::test::OpenLog& log) noexcept {
        TensorMeta meta{};
        meta.set_shape(::crucible::vector_tensor_shape(::crucible::tensor_dim(16)));
        const ::fixy::FixedArray<TensorMeta, 1> record{meta};
        MetaIndex const index = log.try_append_pure(eff::testing::foreground(), crucible::mint_meta_run(record.view()));
        CRUCIBLE_TEST_ASSERT(kTest, index.is_valid());
        CRUCIBLE_TEST_ASSERT(kTest, index.raw() == 0);
    });
}

// The fixtures of the pure gates (test/trace_neg) open their ends through
// ring_ends.h.  The same ends take each pure call with the foreground row, so
// a fixture fails only at the gate of the row that it names.
void test_fixture_ends_take_the_foreground_row() {
    auto const foreground = eff::testing::foreground();
    crucible::test::with_ring_producer([&foreground](TraceRing::Producer& ring) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, TraceRing::try_append_pure(foreground, ring, make_entry(2)));
    });
    crucible::test::with_ring_consumer([&foreground](TraceRing::Consumer& ring) noexcept {
        auto const [drained_count, drained] =
            ::fixy::build_fixed_array<TraceRing::Entry, 4>([&ring, &foreground](auto run) noexcept {
                return TraceRing::drain_pure(foreground, ring, std::move(run));
            });
        CRUCIBLE_TEST_ASSERT(kTest, drained_count == 0u);
    });
    const ::fixy::FixedArray<TensorMeta, 1> record{};
    crucible::MetaRun const records = crucible::mint_meta_run(record.view());
    crucible::test::with_log_producer([&foreground, records](MetaLog::Producer& log) noexcept {
        MetaIndex const index = MetaLog::try_append_pure(foreground, log, records);
        CRUCIBLE_TEST_ASSERT(kTest, index.is_valid() && index.raw() == 0);
    });
}

}  // namespace

int main() {
    test_ring_append_under_the_foreground();
    test_log_append_under_the_foreground();
    test_fixture_ends_take_the_foreground_row();
    std::puts("ok");
    return 0;
}
