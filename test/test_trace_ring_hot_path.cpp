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
concept AppendsToRing = requires(TraceRing& ring, Ctx const& ctx) { ring.try_append_pure(ctx, make_entry(0)); };

// The brand of the run of the check below.  The check names only the type
// of a run, so no run of this brand exists.
struct DrainRunBrand final {};

template <class Ctx>
concept DrainsRing = requires(TraceRing& ring, Ctx const& ctx, ::fixy::ViewMut<TraceRing::Entry, DrainRunBrand> out) {
    ring.drain_pure(ctx, std::move(out));
};

template <class Ctx>
concept AppendsToLog =
    requires(MetaLog& log, Ctx const& ctx) { log.try_append_pure(ctx, ::fixy::View<const TensorMeta>{}); };

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

void test_ring_append_under_the_foreground() {
    ::fixy::Box<TraceRing> ring = ::fixy::mint_box<TraceRing>(eff::testing::test().alloc);
    auto const foreground = eff::testing::foreground();
    CRUCIBLE_TEST_ASSERT(
        kTest, ring->try_append_pure(foreground, make_entry(1), MetaIndex::none(), ScopeHash{0}, CallsiteHash{0}));
    ::fixy::FixedArray<TraceRing::Entry, 4> drained{};
    CRUCIBLE_TEST_ASSERT(kTest, drained.with_mut([&ring, &foreground](auto run) noexcept {
        return ring->drain_pure(foreground, std::move(run));
    }) == 1u);
    CRUCIBLE_TEST_ASSERT(kTest, drained.with_mut([&ring, &foreground](auto run) noexcept {
        return ring->drain_pure(foreground, std::move(run));
    }) == 0u);
}

void test_log_append_under_the_foreground() {
    auto log = std::make_unique<MetaLog>(eff::testing::test().alloc);
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(16);
    meta.strides[0] = ::crucible::tensor_dim(1);
    const ::fixy::FixedArray<TensorMeta, 1> record{meta};
    MetaIndex const index = log->try_append_pure(eff::testing::foreground(), record.view());
    CRUCIBLE_TEST_ASSERT(kTest, index.is_valid());
    CRUCIBLE_TEST_ASSERT(kTest, index.raw() == 0);
}

}  // namespace

int main() {
    test_ring_append_under_the_foreground();
    test_log_append_under_the_foreground();
    std::puts("ok");
    return 0;
}
