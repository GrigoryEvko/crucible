// Storing to the warm tier writes to disk, and that write can block on
// kernel writeback.  The pinned form of the call carries that fact in its
// return type, so a hot-path consumer that will only accept a spin-class
// wait refuses the value at compile time instead of stalling on it.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>

// Opening the store and its open view need a context whose row admits IO
// and Block.
[[nodiscard]] inline ::fixy::TestRunnerCtx store_ctx() {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

using crucible::Cipher;

[[nodiscard]] static Cipher open_cipher(const char* dir) {
    return Cipher::open(store_ctx(), ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{dir}));
}

using crucible::ContentHash;
using crucible::RegionNode;
using crucible::Arena;
using crucible::TraceEntry;
using crucible::SchemaHash;
using crucible::OpIndex;
using crucible::SlotId;
using crucible::TensorMeta;
using crucible::ScalarType;
using ::fixy::Wait;
using ::fixy::WaitStrategy_v;

static auto g_test = ::foundation::effects::testing::test();

static RegionNode* make_test_region(Arena& arena, uint32_t seed) {
    constexpr uint32_t NUM_OPS = 1;
    auto* ops = arena.alloc_array<TraceEntry>(g_test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);
    ops[0].schema_hash = SchemaHash{0xDEADBEEF00000000ULL + seed};
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

static void test_store_pinned_bit_equality(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 1);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);

    ContentHash raw = cipher.store(view, payload);
    auto pinned = cipher.store_pinned(view, payload);
    ContentHash via_wrapper = std::move(pinned).consume();
    assert(raw == via_wrapper);
}

static void test_store_pinned_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 2);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);

    using Got = decltype(cipher.store_pinned(view, payload));
    using Want = Wait<WaitStrategy_v::Block, ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "store_pinned must return Wait<Block, ContentHash>");
    static_assert(::fixy::band_tier_v<Got> == WaitStrategy_v::Block);

    // The result must not be discarded.
    auto p = cipher.store_pinned(view, payload);
    (void)std::move(p).consume();
}

static void test_store_pinned_payload_route(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 3);
    auto cipher = open_cipher(dir);

    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);
    using Got = decltype(cipher.store_pinned(view, payload));
    using Want = Wait<WaitStrategy_v::Block, ContentHash>;
    static_assert(std::is_same_v<Got, Want>);

    auto p = cipher.store_pinned(view, payload);
    ContentHash h = std::move(p).consume();
    assert(static_cast<bool>(h));
}

// Block sits at the bottom of the wait lattice, so it satisfies only
// itself and no stronger requirement.
static void test_block_fence_simulation() {
    using B = Wait<WaitStrategy_v::Block, ContentHash>;
    static_assert(::fixy::satisfies_v<B, WaitStrategy_v::Block>);
    static_assert(!::fixy::satisfies_v<B, WaitStrategy_v::Park>);
    static_assert(!::fixy::satisfies_v<B, WaitStrategy_v::AcquireWait>);
    static_assert(!::fixy::satisfies_v<B, WaitStrategy_v::UmwaitC01>);
    static_assert(!::fixy::satisfies_v<B, WaitStrategy_v::BoundedSpin>);
    static_assert(!::fixy::satisfies_v<B, WaitStrategy_v::SpinPause>);
}

static void test_layout_invariant() {
    static_assert(sizeof(Wait<WaitStrategy_v::Block, ContentHash>) == sizeof(ContentHash));
}

template <typename W>
    requires(::fixy::satisfies_v<W, WaitStrategy_v::Block>)
static ContentHash block_fence_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_block_fence_consumer(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 4);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);

    auto pinned = cipher.store_pinned(view, payload);
    ContentHash h = block_fence_consumer(std::move(pinned));
    assert(static_cast<bool>(h));
}

int main() {
    char tmpdir[] = "/tmp/crucible_cipher_wait_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr);

    test_store_pinned_bit_equality(dir);
    test_store_pinned_type_identity(dir);
    test_store_pinned_payload_route(dir);
    test_block_fence_simulation();
    test_layout_invariant();
    test_e2e_block_fence_consumer(dir);

    std::filesystem::remove_all(dir);
    std::puts("ok");
    return 0;
}
