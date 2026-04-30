// FOUND-G27-AUDIT — Cipher Wait-pinned production surface.
//
// Verifies the store_pinned() variants added to Cipher:
//   - store_pinned(OpenView, region, meta_log) → Wait<Block, ContentHash>
//   - store_pinned(region, meta_log)            → Wait<Block, ContentHash>
//
// The blocking f.write() on the warm-tier NVMe shard is Block-class —
// it may invoke kernel writeback, taking tens-of-μs to milliseconds.
// store_pinned pins this classification so hot-path consumers
// (declaring requires Wait::satisfies<SpinPause>) reject the value
// at compile time.

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Wait.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>

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
using crucible::safety::Wait;
using crucible::safety::WaitStrategy_v;

static crucible::effects::Test g_test;

// Build a minimal RegionNode (mirrors test_cipher.cpp's helper).
static RegionNode* make_test_region(Arena& arena, uint32_t seed) {
    constexpr uint32_t NUM_OPS = 1;
    auto* ops = arena.alloc_array<TraceEntry>(g_test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);
    ops[0].schema_hash = SchemaHash{0xDEADBEEF00000000ULL + seed};
    ops[0].num_inputs  = 1;
    ops[0].num_outputs = 1;
    ops[0].input_metas = arena.alloc_array<TensorMeta>(g_test.alloc, 1);
    ops[0].input_metas[0] = {};
    ops[0].input_metas[0].ndim = 1;
    ops[0].input_metas[0].sizes[0] = 16;
    ops[0].input_metas[0].strides[0] = 1;
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

// ── T01 — store_pinned bit-equality vs raw store ────────────────
static void test_store_pinned_bit_equality(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 1);
    auto cipher = Cipher::open(dir);

    // Raw store + pinned store on the same region must yield the
    // same ContentHash (idempotent — second call hits the
    // already-exists fast path).
    ContentHash raw = cipher.store(region, nullptr);
    auto pinned = cipher.store_pinned(region, nullptr);
    ContentHash via_wrapper = std::move(pinned).consume();
    assert(raw == via_wrapper);
}

// ── T02 — type-identity ──────────────────────────────────────────
static void test_store_pinned_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 2);
    auto cipher = Cipher::open(dir);

    using Got = decltype(cipher.store_pinned(region, nullptr));
    using Want = Wait<WaitStrategy_v::Block, ContentHash>;
    static_assert(std::is_same_v<Got, Want>,
        "store_pinned must return Wait<Block, ContentHash>");
    static_assert(Got::strategy == WaitStrategy_v::Block);

    // Consume so the value isn't discarded.
    auto p = cipher.store_pinned(region, nullptr);
    (void)std::move(p).consume();
}

// ── T03 — typed-view variant returns same Wait-pinned type ──────
static void test_store_pinned_view_overload(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 3);
    auto cipher = Cipher::open(dir);

    auto view = cipher.mint_open_view();
    using Got = decltype(cipher.store_pinned(view, region, nullptr));
    using Want = Wait<WaitStrategy_v::Block, ContentHash>;
    static_assert(std::is_same_v<Got, Want>);

    auto p = cipher.store_pinned(view, region, nullptr);
    ContentHash h = std::move(p).consume();
    assert(static_cast<bool>(h));
}

// ── T04 — fence simulation: Block satisfies only Block-or-weaker
//         (degenerate — Block IS bottom; satisfies only itself) ──
static void test_block_fence_simulation() {
    using B = Wait<WaitStrategy_v::Block, ContentHash>;
    // Block (bottom) satisfies only itself.
    static_assert( B::satisfies<WaitStrategy_v::Block>);
    static_assert(!B::satisfies<WaitStrategy_v::Park>);
    static_assert(!B::satisfies<WaitStrategy_v::AcquireWait>);
    static_assert(!B::satisfies<WaitStrategy_v::UmwaitC01>);
    static_assert(!B::satisfies<WaitStrategy_v::BoundedSpin>);
    static_assert(!B::satisfies<WaitStrategy_v::SpinPause>);
}

// ── T05 — layout invariant ───────────────────────────────────────
static void test_layout_invariant() {
    static_assert(sizeof(Wait<WaitStrategy_v::Block, ContentHash>)
        == sizeof(ContentHash));
}

// ── T06 — end-to-end Block-fence consumer (production-like) ─────
template <typename W>
    requires (W::template satisfies<WaitStrategy_v::Block>)
static ContentHash block_fence_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_block_fence_consumer(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 4);
    auto cipher = Cipher::open(dir);

    auto pinned = cipher.store_pinned(region, nullptr);
    ContentHash h = block_fence_consumer(std::move(pinned));
    assert(static_cast<bool>(h));
}

int main() {
    char tmpdir[] = "/tmp/crucible_cipher_wait_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr);

    test_store_pinned_bit_equality(dir);
    test_store_pinned_type_identity(dir);
    test_store_pinned_view_overload(dir);
    test_block_fence_simulation();
    test_layout_invariant();
    test_e2e_block_fence_consumer(dir);

    // Clean up tmp dir.
    std::filesystem::remove_all(dir);
    std::puts("ok");
    return 0;
}
