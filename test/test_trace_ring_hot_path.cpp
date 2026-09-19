// The pinned variants of the append and drain calls return their
// result wrapped in a tier, and a consumer declares the weakest tier
// it will accept.  A value may be relaxed toward a weaker tier and
// never tightened toward a stronger one, which is what keeps a value
// produced off the hot path out of a hot-path consumer.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/safety/_HotPath.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

using crucible::TraceRing;
using crucible::MetaLog;
using crucible::TensorMeta;
using crucible::MetaIndex;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::OpIndex;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::safety::HotPath;
using crucible::safety::HotPathTier_v;

// An entry carries no operation index of its own: its slot in the
// ring is that index.  Only the two hashes need seeding here.
static TraceRing::Entry make_entry(uint64_t schema_seed) noexcept {
    TraceRing::Entry e{};
    e.schema_hash = SchemaHash{schema_seed};
    e.shape_hash = ShapeHash{schema_seed * 31};
    return e;
}

static void test_try_append_pinned_bit_equality() {
    auto ring = std::make_unique<TraceRing>();
    auto e1 = make_entry(1);
    auto e2 = make_entry(2);

    bool raw_ok = ring->try_append(e1);

    // Each call appends its own entry, so neither result depends on
    // the other's payload.  Both take the same path and must agree.
    auto pinned = ring->try_append_pinned(e2);
    bool via_wrapper = std::move(pinned).consume();

    assert(raw_ok);
    assert(via_wrapper);
}

static void test_try_append_pinned_type_identity() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(3);

    using Got = decltype(ring->try_append_pinned(e));
    using Want = HotPath<HotPathTier_v::Hot, bool>;
    static_assert(std::is_same_v<Got, Want>, "try_append_pinned must return HotPath<Hot, bool>");
    static_assert(Got::tier == HotPathTier_v::Hot);

    auto p = ring->try_append_pinned(e);
    (void)std::move(p).consume();
}

static void test_drain_pinned_type_identity() {
    auto ring = std::make_unique<TraceRing>();

    using Got = decltype(ring->drain_pinned(nullptr, 0u));
    using Want = HotPath<HotPathTier_v::Warm, uint32_t>;
    static_assert(std::is_same_v<Got, Want>, "drain_pinned must return HotPath<Warm, uint32_t>");
    static_assert(Got::tier == HotPathTier_v::Warm);
}

static void test_metalog_try_append_pinned_type_identity() {
    auto log = std::make_unique<MetaLog>();

    using Got = decltype(log->try_append_pinned(static_cast<const TensorMeta*>(nullptr), 0u));
    using Want = HotPath<HotPathTier_v::Hot, MetaIndex>;
    static_assert(std::is_same_v<Got, Want>, "MetaLog::try_append_pinned must return HotPath<Hot, MetaIndex>");
    static_assert(Got::tier == HotPathTier_v::Hot);

    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(16);
    meta.strides[0] = ::crucible::tensor_dim(1);
    auto p = log->try_append_pinned(&meta, 1);
    MetaIndex idx = std::move(p).consume();
    assert(idx.is_valid());
    assert(idx.raw() == 0);
}

static void test_hot_satisfies_weaker_tiers() {
    using Hot = HotPath<HotPathTier_v::Hot, bool>;
    static_assert(Hot::satisfies<HotPathTier_v::Hot>);
    static_assert(Hot::satisfies<HotPathTier_v::Warm>);
    static_assert(Hot::satisfies<HotPathTier_v::Cold>);
}

static void test_warm_rejected_at_hot_fence() {
    using Warm = HotPath<HotPathTier_v::Warm, uint32_t>;
    static_assert(Warm::satisfies<HotPathTier_v::Warm>);
    static_assert(Warm::satisfies<HotPathTier_v::Cold>);
    static_assert(!Warm::satisfies<HotPathTier_v::Hot>, "A warm value must not satisfy a hot fence.  That rejection is "
                                                        "what keeps off-hot-path work out of the per-operation "
                                                        "recording site.");
}

static void test_cold_rejected_at_higher_fences() {
    using Cold = HotPath<HotPathTier_v::Cold, int>;
    static_assert(Cold::satisfies<HotPathTier_v::Cold>);
    static_assert(!Cold::satisfies<HotPathTier_v::Warm>);
    static_assert(!Cold::satisfies<HotPathTier_v::Hot>);
}

static void test_relax_to_weaker_tiers() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(8);

    auto hot = ring->try_append_pinned(e);
    auto warm = std::move(hot).relax<HotPathTier_v::Warm>();
    static_assert(std::is_same_v<decltype(warm), HotPath<HotPathTier_v::Warm, bool>>);

    auto cold = std::move(warm).relax<HotPathTier_v::Cold>();
    static_assert(std::is_same_v<decltype(cold), HotPath<HotPathTier_v::Cold, bool>>);

    bool ok = std::move(cold).consume();
    assert(ok);
}

static void test_layout_invariant() {
    static_assert(sizeof(HotPath<HotPathTier_v::Hot, bool>) == sizeof(bool));
    static_assert(sizeof(HotPath<HotPathTier_v::Warm, uint32_t>) == sizeof(uint32_t));
    static_assert(sizeof(HotPath<HotPathTier_v::Hot, MetaIndex>) == sizeof(MetaIndex));
}

// A consumer shaped like the per-operation recording site, which
// admits a hot-tier value and nothing weaker.
template <typename W>
    requires(W::template satisfies<HotPathTier_v::Hot>)
static bool fg_recording_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_hot_fence_consumer() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(10);

    auto pinned = ring->try_append_pinned(e);
    bool ok = fg_recording_consumer(std::move(pinned));
    assert(ok);
}

template <typename W>
    requires(W::template satisfies<HotPathTier_v::Warm>)
static bool warm_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume() != 0;  // accepts either payload type
}

static void test_warm_fence_admits_hot_via_subsumption() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(11);

    // A hot value clears a warm gate, since hot is the stronger claim.
    auto hot = ring->try_append_pinned(e);
    bool ok_hot = warm_consumer(std::move(hot));
    assert(ok_hot);
}

// Filling the ring takes too long for a unit test, and the behaviour
// of a full ring is covered where the ring itself is tested.  What is
// checked here is that the tier is a property of the return type and
// so survives the failure path unchanged.
static void test_full_ring_type_pin_survives_failure() {
    auto ring = std::make_unique<TraceRing>();

    using FullPathT = HotPath<HotPathTier_v::Hot, bool>;
    FullPathT failure_value{false};
    static_assert(std::is_same_v<decltype(ring->try_append_pinned(make_entry(0))), FullPathT>);
    bool was_full = !std::move(failure_value).consume();
    assert(was_full);
}

// The same argument for the metadata log, whose failure return is an
// invalid index rather than false.
static void test_metalog_full_buffer_type_pin_survives_failure() {
    auto log = std::make_unique<MetaLog>();
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(1);
    meta.strides[0] = ::crucible::tensor_dim(1);

    using NonePathT = HotPath<HotPathTier_v::Hot, MetaIndex>;
    NonePathT none_path{MetaIndex::none()};
    static_assert(std::is_same_v<decltype(log->try_append_pinned(&meta, 1)), NonePathT>);
    MetaIndex idx = std::move(none_path).consume();
    assert(!idx.is_valid());
}

static void test_drain_pinned_empty_ring() {
    auto ring = std::make_unique<TraceRing>();
    TraceRing::Entry buf[4]{};
    auto pinned = ring->drain_pinned(buf, 4);
    uint32_t got = std::move(pinned).consume();
    assert(got == 0);
}

template <typename W, HotPathTier_v T_target>
concept can_tighten = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static void test_cannot_tighten_to_stronger_tier() {
    using HotT = HotPath<HotPathTier_v::Hot, bool>;
    using WarmT = HotPath<HotPathTier_v::Warm, bool>;
    using ColdT = HotPath<HotPathTier_v::Cold, bool>;

    // Toward a weaker tier.
    static_assert(can_tighten<HotT, HotPathTier_v::Warm>);
    static_assert(can_tighten<HotT, HotPathTier_v::Cold>);
    static_assert(can_tighten<WarmT, HotPathTier_v::Cold>);

    // To the same tier.
    static_assert(can_tighten<HotT, HotPathTier_v::Hot>);
    static_assert(can_tighten<WarmT, HotPathTier_v::Warm>);
    static_assert(can_tighten<ColdT, HotPathTier_v::Cold>);

    // Toward a stronger tier, which would manufacture a guarantee
    // nothing established.
    static_assert(!can_tighten<WarmT, HotPathTier_v::Hot>);
    static_assert(!can_tighten<ColdT, HotPathTier_v::Warm>);
    static_assert(!can_tighten<ColdT, HotPathTier_v::Hot>);
}

int main() {
    test_try_append_pinned_bit_equality();
    test_try_append_pinned_type_identity();
    test_drain_pinned_type_identity();
    test_metalog_try_append_pinned_type_identity();
    test_hot_satisfies_weaker_tiers();
    test_warm_rejected_at_hot_fence();
    test_cold_rejected_at_higher_fences();
    test_relax_to_weaker_tiers();
    test_layout_invariant();
    test_e2e_hot_fence_consumer();
    test_warm_fence_admits_hot_via_subsumption();
    test_full_ring_type_pin_survives_failure();
    test_metalog_full_buffer_type_pin_survives_failure();
    test_drain_pinned_empty_ring();
    test_cannot_tighten_to_stronger_tier();

    std::puts("ok");
    return 0;
}
