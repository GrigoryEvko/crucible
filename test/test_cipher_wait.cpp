// Storing to the warm tier writes to disk, and that write can block on
// kernel writeback.  The pinned form of the call carries that fact in its
// return type, so a hot-path consumer that will only accept a spin-class
// wait refuses the value at compile time instead of stalling on it.

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <foundation/effects/Effect.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

// Opening the store and its open view need a context whose row admits IO
// and Block.
[[nodiscard]] inline ::fixy::TestRunnerCtx store_ctx() {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

using crucible::Cipher;

// The text of the root copies the characters of the directory.
[[nodiscard]] static Cipher open_cipher(const char* dir) {
    ::foundation::core::GrowText root = ::foundation::core::mint_grow_text(Cipher::MAX_ROOT_PATH_BYTES);
    for (std::size_t index = 0; dir[index] != '\0'; ++index) {
        root.append(store_ctx().cap().alloc, dir[index]).expect("the text holds each character of the directory");
    }
    ::foundation::core::HeapText const root_path = ::foundation::core::freeze(std::move(root));
    return Cipher::open(store_ctx(), store_ctx().cap().alloc,
                        ::fixy::mint_tagged<::fixy::tags::source::External>(root_path.view()))
        .expect("the test opens its store");
}

using crucible::ContentHash;
using crucible::RegionNode;
using crucible::TraceEntry;
using crucible::SchemaHash;
using crucible::OpIndex;
using crucible::SlotId;
using crucible::TensorMeta;
using crucible::ScalarType;
using ::fixy::Wait;
using ::fixy::WaitStrategy_v;

static auto g_test = ::foundation::effects::testing::test();

// The ops, their arrays and the region are in dag_arena.
static ::fixy::Ref<RegionNode> make_test_region(::fixy::BumpArena<crucible::DagArena>& dag_arena, uint32_t seed) {
    constexpr uint32_t NUM_OPS = 1;
    const auto ops_run = dag_arena.array<TraceEntry>(g_test.alloc, NUM_OPS, TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    const ::fixy::View<TraceEntry> ops = ops_run.view();
    TraceEntry& op = *ops.get(0).expect("the region holds one op");
    op.schema_hash = SchemaHash{0xDEADBEEF00000000ULL + seed};
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(16);
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.dtype = ScalarType::Float;
    op.set_inputs(::fixy::check::run_of(kTest, g_test.alloc, dag_arena, meta),
                  ::fixy::check::run_of(kTest, g_test.alloc, dag_arena, OpIndex{}),
                  ::fixy::check::run_of(kTest, g_test.alloc, dag_arena, SlotId{}));
    op.set_outputs(::fixy::check::run_of(kTest, g_test.alloc, dag_arena, meta),
                   ::fixy::check::run_of(kTest, g_test.alloc, dag_arena, SlotId{seed}));
    return crucible::make_region(g_test.alloc, dag_arena, ops);
}

static void test_store_pinned_bit_equality(const char* dir) {
    ::fixy::BumpArena<crucible::DagArena> dag_arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{size_t{1} << 16});
    const ::fixy::Ref<RegionNode> region = make_test_region(dag_arena, 1);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);

    ContentHash raw = cipher.store(store_ctx(), view, payload);
    auto pinned = cipher.store_pinned(store_ctx(), view, payload);
    ContentHash via_wrapper = std::move(pinned).consume();
    CRUCIBLE_TEST_ASSERT(kTest, raw == via_wrapper);
}

static void test_store_pinned_type_identity(const char* dir) {
    ::fixy::BumpArena<crucible::DagArena> dag_arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{size_t{1} << 16});
    const ::fixy::Ref<RegionNode> region = make_test_region(dag_arena, 2);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);

    using Got = decltype(cipher.store_pinned(store_ctx(), view, payload));
    using Want = Wait<WaitStrategy_v::Block, ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "store_pinned must return Wait<Block, ContentHash>");
    static_assert(::fixy::band_tier_v<Got> == WaitStrategy_v::Block);

    // The result must not be discarded.
    auto p = cipher.store_pinned(store_ctx(), view, payload);
    (void)std::move(p).consume();
}

static void test_store_pinned_payload_route(const char* dir) {
    ::fixy::BumpArena<crucible::DagArena> dag_arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{size_t{1} << 16});
    const ::fixy::Ref<RegionNode> region = make_test_region(dag_arena, 3);
    auto cipher = open_cipher(dir);

    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);
    using Got = decltype(cipher.store_pinned(store_ctx(), view, payload));
    using Want = Wait<WaitStrategy_v::Block, ContentHash>;
    static_assert(std::is_same_v<Got, Want>);

    auto p = cipher.store_pinned(store_ctx(), view, payload);
    ContentHash h = std::move(p).consume();
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(h));
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
    ::fixy::BumpArena<crucible::DagArena> dag_arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(Bytes{size_t{1} << 16});
    const ::fixy::Ref<RegionNode> region = make_test_region(dag_arena, 4);
    auto cipher = open_cipher(dir);
    auto view = cipher.mint_open_view(store_ctx());
    auto payload = Cipher::content_addressed(region);

    auto pinned = cipher.store_pinned(store_ctx(), view, payload);
    ContentHash h = block_fence_consumer(std::move(pinned));
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(h));
}

int main() {
    char tmpdir[] = "/tmp/crucible_cipher_wait_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    CRUCIBLE_TEST_ASSERT(kTest, dir != nullptr);

    test_store_pinned_bit_equality(dir);
    test_store_pinned_type_identity(dir);
    test_store_pinned_payload_route(dir);
    test_block_fence_simulation();
    test_layout_invariant();
    test_e2e_block_fence_consumer(dir);

    std::filesystem::remove_all(dir);
    ::fixy::check::pass(kTest, "ok\n");
    return 0;
}
