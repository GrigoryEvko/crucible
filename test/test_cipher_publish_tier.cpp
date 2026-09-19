// Each publication entry point returns a value carrying its tier in the
// type, so a consumer that requires a particular tier rejects the wrong
// one at compile time.  The tiers order Hot above Warm above Cold, and a
// value may weaken its claim but never strengthen it.
//
// Only the warm path writes anything today.  The hot and cold paths
// return a none-hash of the right type, which is enough to pin the type
// surface and the fence behaviour, and the assertions below say so
// wherever the returned hash is read.

#include <crucible/Cipher.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/safety/CipherTier.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <type_traits>
#include <utility>

using CipherRoot = crucible::fixy::wrap::Path<crucible::fixy::tags::source::External>;

using crucible::Cipher;
using crucible::ContentHash;
using crucible::RegionNode;
using crucible::Arena;
using crucible::TraceEntry;
using crucible::SchemaHash;
using crucible::OpIndex;
using crucible::SlotId;
using crucible::TensorMeta;
using crucible::ScalarType;
using crucible::safety::CipherTier;
using crucible::safety::CipherTierTag_v;

static auto g_test = crucible::effects::testing::test();

// The seed varies the schema hash, so every test gets a region with its
// own content hash and writes its own file.
static RegionNode* make_test_region(Arena& arena, uint32_t seed) {
    constexpr uint32_t NUM_OPS = 1;
    auto* ops = arena.alloc_array<TraceEntry>(g_test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);
    ops[0].schema_hash = SchemaHash{0xCAFEBABE00000000ULL + seed};
    ops[0].num_inputs = 1;
    ops[0].num_outputs = 1;
    ops[0].input_metas = arena.alloc_array<TensorMeta>(g_test.alloc, 1);
    ops[0].input_metas[0] = {};
    ops[0].input_metas[0].ndim = 1;
    ops[0].input_metas[0].sizes[0] = ::crucible::tensor_dim(16);
    ops[0].input_metas[0].strides[0] = ::crucible::tensor_dim(1);
    ops[0].input_metas[0].dtype = ScalarType::Float;
    ops[0].output_metas = arena.alloc_array<TensorMeta>(g_test.alloc, 1);
    ops[0].output_metas[0] = ops[0].input_metas[0];
    ops[0].input_trace_indices = arena.alloc_array<OpIndex>(g_test.alloc, 1);
    ops[0].input_trace_indices[0] = OpIndex{};
    ops[0].input_slot_ids = arena.alloc_array<SlotId>(g_test.alloc, 1);
    ops[0].input_slot_ids[0] = SlotId{};
    ops[0].output_slot_ids = arena.alloc_array<SlotId>(g_test.alloc, 1);
    ops[0].output_slot_ids[0] = SlotId{seed};
    return crucible::make_region(g_test.alloc, arena, ops, NUM_OPS);
}

static void test_publish_warm_bit_equality(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 1);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    // The second write of the same region takes the already-exists
    // path, so publishing after storing must still report the hash the
    // raw store produced.
    ContentHash raw = cipher.store(view, payload, nullptr);
    auto warm = cipher.publish_warm(view, payload, nullptr);
    ContentHash via_wrapper = std::move(warm).consume();
    assert(raw == via_wrapper);
    assert(static_cast<bool>(raw));
}

static void test_publish_warm_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 2);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    using Got = decltype(cipher.publish_warm(view, payload, nullptr));
    using Want = CipherTier<CipherTierTag_v::Warm, ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "publish_warm must return CipherTier<Warm, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Warm);

    auto p = cipher.publish_warm(view, payload, nullptr);
    (void)std::move(p).consume();
}

static void test_publish_hot_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 3);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    using Got = decltype(cipher.publish_hot(view, payload, nullptr));
    using Want = CipherTier<CipherTierTag_v::Hot, ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "publish_hot must return CipherTier<Hot, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Hot);

    // The hot path writes nothing yet, so the hash it returns is none.
    auto p = cipher.publish_hot(view, payload, nullptr);
    ContentHash h = std::move(p).consume();
    assert(!static_cast<bool>(h));
}

static void test_publish_cold_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 4);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    using Got = decltype(cipher.publish_cold(view, payload, nullptr));
    using Want = CipherTier<CipherTierTag_v::Cold, ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "publish_cold must return CipherTier<Cold, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Cold);

    // The cold path writes nothing yet, so the hash it returns is none.
    auto p = cipher.publish_cold(view, payload, nullptr);
    ContentHash h = std::move(p).consume();
    assert(!static_cast<bool>(h));
}

static void test_view_and_payload_route(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 5);
    auto cipher = Cipher::open(CipherRoot{dir});

    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    using WarmGot = decltype(cipher.publish_warm(view, payload, nullptr));
    static_assert(std::is_same_v<WarmGot, CipherTier<CipherTierTag_v::Warm, ContentHash>>);

    using HotGot = decltype(cipher.publish_hot(view, payload, nullptr));
    static_assert(std::is_same_v<HotGot, CipherTier<CipherTierTag_v::Hot, ContentHash>>);

    using ColdGot = decltype(cipher.publish_cold(view, payload, nullptr));
    static_assert(std::is_same_v<ColdGot, CipherTier<CipherTierTag_v::Cold, ContentHash>>);

    (void)std::move(cipher.publish_warm(view, payload, nullptr)).consume();
    (void)std::move(cipher.publish_hot(view, payload, nullptr)).consume();
    (void)std::move(cipher.publish_cold(view, payload, nullptr)).consume();
}

static void test_hot_satisfies_weaker_tiers() {
    using Hot = CipherTier<CipherTierTag_v::Hot, ContentHash>;

    static_assert(Hot::satisfies<CipherTierTag_v::Hot>);
    static_assert(Hot::satisfies<CipherTierTag_v::Warm>);
    static_assert(Hot::satisfies<CipherTierTag_v::Cold>);
}

static void test_warm_rejected_at_hot_fence() {
    using Warm = CipherTier<CipherTierTag_v::Warm, ContentHash>;

    static_assert(Warm::satisfies<CipherTierTag_v::Warm>);
    static_assert(Warm::satisfies<CipherTierTag_v::Cold>);
    static_assert(!Warm::satisfies<CipherTierTag_v::Hot>, "Warm must not satisfy Hot.  The hot-tier reincarnation gate "
                                                          "depends on this rejection.");
}

static void test_cold_rejected_at_higher_fences() {
    using Cold = CipherTier<CipherTierTag_v::Cold, ContentHash>;

    static_assert(Cold::satisfies<CipherTierTag_v::Cold>);
    static_assert(!Cold::satisfies<CipherTierTag_v::Warm>);
    static_assert(!Cold::satisfies<CipherTierTag_v::Hot>);
}

static void test_relax_to_weaker_tiers(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 9);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    // Relaxing twice in succession, to show the weakening composes and
    // carries the same value the whole way down.
    auto hot = cipher.publish_hot(view, payload, nullptr);
    auto warm = std::move(hot).relax<CipherTierTag_v::Warm>();
    static_assert(std::is_same_v<decltype(warm), CipherTier<CipherTierTag_v::Warm, ContentHash>>);

    auto cold = std::move(warm).relax<CipherTierTag_v::Cold>();
    static_assert(std::is_same_v<decltype(cold), CipherTier<CipherTierTag_v::Cold, ContentHash>>);

    ContentHash h = std::move(cold).consume();
    (void)h;
}

static void test_layout_invariant() {
    static_assert(sizeof(CipherTier<CipherTierTag_v::Hot, ContentHash>) == sizeof(ContentHash));
    static_assert(sizeof(CipherTier<CipherTierTag_v::Warm, ContentHash>) == sizeof(ContentHash));
    static_assert(sizeof(CipherTier<CipherTierTag_v::Cold, ContentHash>) == sizeof(ContentHash));
}

// The shape of the hot-tier reincarnation admission gate: the tier
// requirement is part of the signature, so a weaker value never reaches
// the body.
template <typename W>
    requires(W::template satisfies<CipherTierTag_v::Hot>)
static ContentHash hot_reshard_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_hot_fence_consumer(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 11);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    auto pinned = cipher.publish_hot(view, payload, nullptr);
    ContentHash h = hot_reshard_consumer(std::move(pinned));
    // Passing the gate is the property under test.  The hash is none
    // because the hot path writes nothing yet.
    assert(!static_cast<bool>(h));
}

template <typename W>
    requires(W::template satisfies<CipherTierTag_v::Warm>)
static ContentHash warm_publish_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_warm_fence_admits_hot_and_warm(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 12);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    auto warm_val = cipher.publish_warm(view, payload, nullptr);
    ContentHash h_warm = warm_publish_consumer(std::move(warm_val));
    assert(static_cast<bool>(h_warm));

    // A stronger tier passes the same gate.
    auto hot_val = cipher.publish_hot(view, payload, nullptr);
    ContentHash h_hot = warm_publish_consumer(std::move(hot_val));
    (void)h_hot;
}

static void test_phase5_stub_semantics(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 13);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    static_assert(noexcept(cipher.publish_hot(view, payload, nullptr)));
    static_assert(noexcept(cipher.publish_cold(view, payload, nullptr)));

    // These two assertions hold only while the hot and cold paths write
    // nothing.  Giving either one a real backend makes them fail, and
    // the check belongs with that change rather than here.
    auto hot = cipher.publish_hot(view, payload, nullptr);
    auto cold = cipher.publish_cold(view, payload, nullptr);
    ContentHash h_hot = std::move(hot).consume();
    ContentHash h_cold = std::move(cold).consume();
    assert(!static_cast<bool>(h_hot));
    assert(!static_cast<bool>(h_cold));
}

// Drift attribution has to tell a hot-tier problem apart from a
// cold-storage latency spike.  The tier is a static member, so the
// classification happens at compile time and no runtime tag field is
// needed on the value.
template <typename W>
[[nodiscard]] static constexpr int classify_tier_for_drift_attribution() noexcept {
    if constexpr (W::tier == CipherTierTag_v::Hot) return 1;
    if constexpr (W::tier == CipherTierTag_v::Warm) return 2;
    if constexpr (W::tier == CipherTierTag_v::Cold) return 3;
    return 0;
}

static void test_runtime_tier_reader_pattern(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 14);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    auto warm_v = cipher.publish_warm(view, payload, nullptr);
    auto hot_v = cipher.publish_hot(view, payload, nullptr);
    auto cold_v = cipher.publish_cold(view, payload, nullptr);

    static_assert(classify_tier_for_drift_attribution<decltype(warm_v)>() == 2);
    static_assert(classify_tier_for_drift_attribution<decltype(hot_v)>() == 1);
    static_assert(classify_tier_for_drift_attribution<decltype(cold_v)>() == 3);

    (void)std::move(warm_v).consume();
    (void)std::move(hot_v).consume();
    (void)std::move(cold_v).consume();
}

// Replay takes the opposite polarity from the hot-reshard gate.  Its
// canonical source is the cold archive, and a gate on the bottom of the
// lattice is the weakest one there is, so it admits every tier.
template <typename W>
    requires(W::template satisfies<CipherTierTag_v::Cold>)
static ContentHash replay_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_replay_engine_admits_all_tiers(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 15);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    auto cold = cipher.publish_cold(view, payload, nullptr);
    (void)replay_consumer(std::move(cold));

    auto warm = cipher.publish_warm(view, payload, nullptr);
    ContentHash h = replay_consumer(std::move(warm));
    assert(static_cast<bool>(h));

    auto hot = cipher.publish_hot(view, payload, nullptr);
    (void)replay_consumer(std::move(hot));
}

// One region published to all three tiers in a single iteration, the
// way a real publication cycle does it.  Each call yields its own type,
// so the three results cannot be confused for one another even though
// they describe the same region.
static void test_sequential_three_tier_publish(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 16);
    auto cipher = Cipher::open(CipherRoot{dir});

    auto view = cipher.mint_open_view();
    auto payload = Cipher::content_addressed(region);

    // Publication runs from the fastest tier to the most durable, so
    // that a peer can serve the region before the archive write
    // finishes.
    auto h_pub = cipher.publish_hot(view, payload, nullptr);
    auto w_pub = cipher.publish_warm(view, payload, nullptr);
    auto c_pub = cipher.publish_cold(view, payload, nullptr);

    static_assert(!std::is_same_v<decltype(h_pub), decltype(w_pub)>);
    static_assert(!std::is_same_v<decltype(w_pub), decltype(c_pub)>);
    static_assert(!std::is_same_v<decltype(h_pub), decltype(c_pub)>);

    ContentHash h_hash = std::move(h_pub).consume();
    ContentHash w_hash = std::move(w_pub).consume();
    ContentHash c_hash = std::move(c_pub).consume();

    // Only the warm publication has written anything.
    assert(!static_cast<bool>(h_hash));
    assert(static_cast<bool>(w_hash));
    assert(!static_cast<bool>(c_hash));
}

// Relaxing weakens a claim, and no argument to it can strengthen one.
// The header asserts this too; the detector below re-derives it through
// substitution failure at a call site, which is how a caller would meet
// the rejection.
template <typename W, CipherTierTag_v T_target>
concept can_tighten = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static void test_cannot_tighten_to_stronger_tier() {
    using HotT = CipherTier<CipherTierTag_v::Hot, ContentHash>;
    using WarmT = CipherTier<CipherTierTag_v::Warm, ContentHash>;
    using ColdT = CipherTier<CipherTierTag_v::Cold, ContentHash>;

    static_assert(can_tighten<HotT, CipherTierTag_v::Warm>);
    static_assert(can_tighten<HotT, CipherTierTag_v::Cold>);
    static_assert(can_tighten<WarmT, CipherTierTag_v::Cold>);

    static_assert(can_tighten<HotT, CipherTierTag_v::Hot>);
    static_assert(can_tighten<WarmT, CipherTierTag_v::Warm>);
    static_assert(can_tighten<ColdT, CipherTierTag_v::Cold>);

    static_assert(!can_tighten<WarmT, CipherTierTag_v::Hot>);
    static_assert(!can_tighten<ColdT, CipherTierTag_v::Warm>);
    static_assert(!can_tighten<ColdT, CipherTierTag_v::Hot>);
}

static void test_content_addressed_publish_overloads(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 18);
    const auto payload = Cipher::content_addressed(region);
    auto cipher = Cipher::open(CipherRoot{dir});
    auto view = cipher.mint_open_view();

    using Payload = decltype(payload);
    static_assert(crucible::safety::proto::is_content_addressed_v<typename Payload::payload_type>);

    using WarmGot = decltype(cipher.publish_warm(view, payload, nullptr));
    using HotGot = decltype(cipher.publish_hot(view, payload, nullptr));
    using ColdGot = decltype(cipher.publish_cold(view, payload, nullptr));
    static_assert(std::is_same_v<WarmGot, CipherTier<CipherTierTag_v::Warm, ContentHash>>);
    static_assert(std::is_same_v<HotGot, CipherTier<CipherTierTag_v::Hot, ContentHash>>);
    static_assert(std::is_same_v<ColdGot, CipherTier<CipherTierTag_v::Cold, ContentHash>>);

    ContentHash warm_hash = std::move(cipher.publish_warm(view, payload, nullptr)).consume();
    assert(static_cast<bool>(warm_hash));
    (void)std::move(cipher.publish_hot(view, payload, nullptr)).consume();
    (void)std::move(cipher.publish_cold(view, payload, nullptr)).consume();
}

int main() {
    char tmpdir[] = "/tmp/crucible_cipher_publish_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr);

    test_publish_warm_bit_equality(dir);
    test_publish_warm_type_identity(dir);
    test_publish_hot_type_identity(dir);
    test_publish_cold_type_identity(dir);
    test_view_and_payload_route(dir);
    test_hot_satisfies_weaker_tiers();
    test_warm_rejected_at_hot_fence();
    test_cold_rejected_at_higher_fences();
    test_relax_to_weaker_tiers(dir);
    test_layout_invariant();
    test_e2e_hot_fence_consumer(dir);
    test_e2e_warm_fence_admits_hot_and_warm(dir);
    test_phase5_stub_semantics(dir);
    test_runtime_tier_reader_pattern(dir);
    test_replay_engine_admits_all_tiers(dir);
    test_sequential_three_tier_publish(dir);
    test_cannot_tighten_to_stronger_tier();
    test_content_addressed_publish_overloads(dir);

    std::filesystem::remove_all(dir);
    std::puts("ok");
    return 0;
}
