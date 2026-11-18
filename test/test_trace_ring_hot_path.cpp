// The tier of the hot path is a view of the effect row of a context, and
// no value stores it.  The pure variants of the append and drain calls of
// the ring and the metadata log take a context whose row is pure, so the
// view gives each such context the tier Hot.  A context whose row holds
// Alloc has the tier Warm, and the pure variants refuse it.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/Bands.h>
#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <memory>

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

template <class Ctx>
concept DrainsRing =
    requires(TraceRing& ring, Ctx const& ctx) { ring.drain_pure(ctx, ::fixy::View<TraceRing::Entry>{}); };

template <class Ctx>
concept AppendsToLog =
    requires(MetaLog& log, Ctx const& ctx) { log.try_append_pure(ctx, ::fixy::View<const TensorMeta>{}); };

// The foreground row is hot, and each pure variant takes it.  A row with
// Alloc is warm, and each pure variant refuses it.
static_assert(hot_path_tier_v<eff::row_type_of_t<ForegroundCtx>> == HotPathTier_v::Hot);
static_assert(hot_path_tier_v<eff::row_type_of_t<AllocCtx>> == HotPathTier_v::Warm);
static_assert(AppendsToRing<ForegroundCtx> && DrainsRing<ForegroundCtx> && AppendsToLog<ForegroundCtx>);
static_assert(!AppendsToRing<AllocCtx> && !DrainsRing<AllocCtx> && !AppendsToLog<AllocCtx>);

// A hot row meets a warm or a cold budget, and a warm row does not meet a
// hot budget.
static_assert(::fixy::HotPathLattice::leq(HotPathTier_v::Warm, hot_path_tier_v<eff::Row<>>));
static_assert(!::fixy::HotPathLattice::leq(HotPathTier_v::Hot, hot_path_tier_v<eff::Row<eff::Effect::Alloc>>));

void test_ring_append_under_the_foreground() {
    auto ring = std::make_unique<TraceRing>();
    auto const foreground = eff::testing::foreground();
    assert(ring->try_append_pure(foreground, make_entry(1), MetaIndex::none(), ScopeHash{0}, CallsiteHash{0}));
    ::fixy::FixedArray<TraceRing::Entry, 4> drained{};
    assert(ring->drain_pure(foreground, drained.view()) == 1u);
    assert(ring->drain_pure(foreground, drained.view()) == 0u);
}

void test_log_append_under_the_foreground() {
    auto log = std::make_unique<MetaLog>(eff::testing::test().alloc);
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(16);
    meta.strides[0] = ::crucible::tensor_dim(1);
    const ::fixy::FixedArray<TensorMeta, 1> record{meta};
    MetaIndex const index = log->try_append_pure(eff::testing::foreground(), record.view());
    assert(index.is_valid());
    assert(index.raw() == 0);
}

}  // namespace

int main() {
    test_ring_append_under_the_foreground();
    test_log_append_under_the_foreground();
    std::puts("ok");
    return 0;
}
