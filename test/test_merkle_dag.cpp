#include <crucible/MerkleDag.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/IsSwmrHandle.h>
#include "test_assert.h"
#include <cstdio>
#include <cstring>
#include <type_traits>

using crucible::SchemaHash;
using crucible::ContentHash;
using crucible::MerkleHash;

namespace {

[[nodiscard]] crucible::CompiledKernel* kernel_ptr(void* p) noexcept {
    return static_cast<crucible::CompiledKernel*>(p);
}

}  // namespace

[[gnu::cold]] int main() {
    auto test = crucible::effects::testing::test();
    crucible::Arena arena(1 << 16);

    crucible::TensorMeta m{};
    m.ndim = 2;
    m.sizes[0] = ::crucible::tensor_dim(32);
    m.sizes[1] = ::crucible::tensor_dim(64);
    m.strides[0] = ::crucible::tensor_dim(64);
    m.strides[1] = ::crucible::tensor_dim(1);
    m.dtype = crucible::ScalarType::Float;
    auto nbytes = crucible::compute_storage_nbytes(crucible::external_tensor_meta(m));
    auto nbytes_det = crucible::compute_storage_nbytes_det(crucible::external_tensor_meta(m));
    static_assert(
        std::is_same_v<decltype(nbytes_det), crucible::safety::DetSafe<crucible::safety::DetSafeTier_v::Pure,
                                                                       crucible::safety::Saturated<uint64_t>>>);
    assert(nbytes_det.peek() == nbytes);
    // (31*64 + 63*1 + 1) * 4 = 2048 * 4 = 8192 (= 32 * 64 * sizeof(float))
    assert(nbytes.value() == 8192);
    assert(!nbytes.was_clamped() && "well-formed tensor must not saturate");

    {
        crucible::TensorMeta huge{};
        huge.ndim = 2;
        huge.sizes[0] = ::crucible::tensor_dim(1LL << 32);
        huge.strides[0] = ::crucible::tensor_dim(1LL << 32);  // 2^32 × 2^32 = 2^64, overflows int64_t
        huge.sizes[1] = ::crucible::tensor_dim(1);
        huge.strides[1] = ::crucible::tensor_dim(1);
        huge.dtype = crucible::ScalarType::Float;
        auto nb = crucible::compute_storage_nbytes(crucible::external_tensor_meta(huge));
        assert(nb.value() == UINT64_MAX && "huge tensor must saturate value to UINT64_MAX");
        assert(nb.was_clamped() && "huge tensor must carry clamped=true");
    }

    crucible::TraceEntry ops[3]{};
    ops[0].schema_hash = SchemaHash{0xAABB};
    ops[1].schema_hash = SchemaHash{0xCCDD};
    ops[2].schema_hash = SchemaHash{0xEEFF};
    auto* region = crucible::make_region(test.alloc, arena, ops, 3);
    assert(region != nullptr);
    assert(region->kind == crucible::TraceNodeKind::REGION);
    assert(region->num_ops == 3);
    assert(region->ops[0].schema_hash == SchemaHash{0xAABB});
    assert(region->ops[2].schema_hash == SchemaHash{0xEEFF});
    assert(region->first_op_schema == SchemaHash{0xAABB});
    assert(static_cast<bool>(region->content_hash));

    ContentHash h1 = region->content_hash;
    auto* region2 = crucible::make_region(test.alloc, arena, ops, 3);
    assert(region2->content_hash == h1);

    auto* terminal = crucible::make_terminal(test.alloc, arena);
    assert(terminal->kind == crucible::TraceNodeKind::TERMINAL);
    assert(terminal->next == nullptr);

    region->next = terminal;
    crucible::recompute_merkle(region);
    assert(static_cast<bool>(region->merkle_hash));

    // RowHash{0} is the row identity of a bare, unwrapped type, so it is
    // the row every untyped call site keys on.
    crucible::KernelCache cache;
    using crucible::RowHash;
    using KernelSlot = crucible::KernelCache::KernelCacheSlot;
    using SlotSnapshot = crucible::KernelCache::KernelCacheSlotSnapshot;
    static_assert(sizeof(KernelSlot) == 24);
    static_assert(alignof(KernelSlot) == 8);
    static_assert(sizeof(SlotSnapshot) == 24);
    static_assert(crucible::safety::extract::IsSwmrWriter<KernelSlot::WriterHandle>);
    static_assert(crucible::safety::extract::IsSwmrReader<KernelSlot::ReaderHandle>);
    static_assert(
        std::is_same_v<crucible::safety::extract::swmr_writer_value_t<KernelSlot::WriterHandle>, SlotSnapshot>);
    static_assert(
        std::is_same_v<crucible::safety::extract::swmr_reader_value_t<KernelSlot::ReaderHandle>, SlotSnapshot>);

    assert(cache.lookup(ContentHash{0x1234}, RowHash{0}) == nullptr);
    struct FakeKernel {
        int x;
    };
    FakeKernel fk{42};
    assert(cache.insert(ContentHash{0x1234}, RowHash{0}, kernel_ptr(&fk)).has_value());
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0}) == kernel_ptr(&fk));
    FakeKernel fk2{99};
    assert(cache.insert(ContentHash{0x1234}, RowHash{0}, kernel_ptr(&fk2)).has_value());
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0}) == kernel_ptr(&fk2));

    FakeKernel fk_row{777};
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA}) == nullptr);
    assert(cache.insert(ContentHash{0x1234}, RowHash{0xAA}, kernel_ptr(&fk_row)).has_value());
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA}) == kernel_ptr(&fk_row));
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0}) == kernel_ptr(&fk2));
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0xBB}) == nullptr);

    // The probe distinguishes a variant update from a sibling row: an
    // insert under an occupied (content, row) replaces that slot alone.
    FakeKernel fk_row_v2{888};
    assert(cache.insert(ContentHash{0x1234}, RowHash{0xAA}, kernel_ptr(&fk_row_v2)).has_value());
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA}) == kernel_ptr(&fk_row_v2));
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0}) == kernel_ptr(&fk2));

    // Row discrimination is not bound to one content_hash slot index.
    FakeKernel fk_other{555};
    assert(cache.insert(ContentHash{0x9999}, RowHash{0xAA}, kernel_ptr(&fk_other)).has_value());
    assert(cache.lookup(ContentHash{0x9999}, RowHash{0xAA}) == kernel_ptr(&fk_other));
    assert(cache.lookup(ContentHash{0x9999}, RowHash{0}) == nullptr);
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0xCC}) == nullptr);

    // The sentinel row is reserved for end-of-region markers elsewhere.
    // The cache must treat it as an ordinary 64-bit row identity.
    FakeKernel fk_sentinel{0xFEED};
    assert(cache.lookup(ContentHash{0x1234}, RowHash::sentinel()) == nullptr);
    assert(cache.insert(ContentHash{0x1234}, RowHash::sentinel(), kernel_ptr(&fk_sentinel)).has_value());
    assert(cache.lookup(ContentHash{0x1234}, RowHash::sentinel()) == kernel_ptr(&fk_sentinel));
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0}) == kernel_ptr(&fk2));
    assert(cache.lookup(ContentHash{0x1234}, RowHash{0xAA}) == kernel_ptr(&fk_row_v2));

    // Many rows under one content_hash drive the probe chain around
    // the table.
    {
        crucible::KernelCache row_pressure_cache(/*capacity=*/16);
        constexpr uint32_t N = 8;  // half the table — leaves room for probes
        FakeKernel row_kernels[N];
        for (uint32_t i = 0; i < N; ++i) {
            row_kernels[i] = FakeKernel{static_cast<int>(0x1000 + i)};
            assert(row_pressure_cache
                       .insert(ContentHash{0xABCDEF},  // one shared content
                               RowHash{0x100 + i},  // distinct rows
                               kernel_ptr(&row_kernels[i]))
                       .has_value());
        }
        for (uint32_t i = 0; i < N; ++i) {
            auto* k = row_pressure_cache.lookup(ContentHash{0xABCDEF}, RowHash{0x100 + i});
            assert(k == kernel_ptr(&row_kernels[i]));
        }
        // The probe terminates on an absent row instead of matching a
        // foreign-row sibling that shares the content hash.
        assert(row_pressure_cache.lookup(ContentHash{0xABCDEF}, RowHash{0xDEAD}) == nullptr);
    }

    {
        crucible::KernelCache tiny_cache(/*capacity=*/4);
        FakeKernel small_kernels[5];
        for (uint32_t i = 0; i < 4; ++i) {
            small_kernels[i] = FakeKernel{static_cast<int>(i)};
            assert(tiny_cache.insert(ContentHash{0x42}, RowHash{0xA00 + i}, kernel_ptr(&small_kernels[i])).has_value());
        }
        // The fifth insert finds no empty slot: all four hold the same
        // content under a different row, so none is a variant update.
        small_kernels[4] = FakeKernel{4};
        auto fifth = tiny_cache.insert(ContentHash{0x42}, RowHash{0xA04},  // novel row
                                       kernel_ptr(&small_kernels[4]));
        assert(!fifth.has_value());
        assert(fifth.error() == crucible::KernelCache::InsertError::TableFull);
        // A full table still accepts a variant update.  TableFull is
        // returned only when no (content, row) match exists.
        FakeKernel variant_replacement{0xC};
        auto variant = tiny_cache.insert(ContentHash{0x42}, RowHash{0xA00},  // existing row
                                         kernel_ptr(&variant_replacement));
        assert(variant.has_value());
        assert(tiny_cache.lookup(ContentHash{0x42}, RowHash{0xA00}) == kernel_ptr(&variant_replacement));
    }

    // RowHash{0} is a lookup target like any other row.  This sequence
    // is the single-threaded stand-in for a concurrent hazard: an
    // inserter that observes a claimed-but-unpublished slot for content
    // C must not mistake it for the (C, 0) slot and silently lose the
    // pending kernel.  Inserting under row 0, then a second row, then
    // row 0 again reproduces the same slot observation the race makes.
    {
        crucible::KernelCache audit2_cache(/*capacity=*/16);
        FakeKernel k_row0_v1{1};
        FakeKernel k_rowR_v1{2};
        FakeKernel k_row0_v2{3};

        assert(audit2_cache.insert(ContentHash{0x55AA}, RowHash{0}, kernel_ptr(&k_row0_v1)).has_value());

        assert(audit2_cache.insert(ContentHash{0x55AA}, RowHash{0xBEEF}, kernel_ptr(&k_rowR_v1)).has_value());

        assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0}) == kernel_ptr(&k_row0_v1));
        assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0xBEEF}) == kernel_ptr(&k_rowR_v1));

        assert(audit2_cache.insert(ContentHash{0x55AA}, RowHash{0}, kernel_ptr(&k_row0_v2)).has_value());

        assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0}) == kernel_ptr(&k_row0_v2));
        assert(audit2_cache.lookup(ContentHash{0x55AA}, RowHash{0xBEEF}) == kernel_ptr(&k_rowR_v1));
    }

    assert(crucible::element_size(crucible::ScalarType::Float) == crucible::ElementBytes{4});
    assert(crucible::element_size(crucible::ScalarType::Double) == crucible::ElementBytes{8});
    assert(crucible::element_size(crucible::ScalarType::Half) == crucible::ElementBytes{2});
    assert(crucible::element_size(crucible::ScalarType::Byte) == crucible::ElementBytes{1});
    assert(crucible::element_size(crucible::ScalarType::ComplexDouble) == crucible::ElementBytes{16});

    crucible::TraceEntry body_ops1[2]{};
    body_ops1[0].schema_hash = SchemaHash{0x1111};
    body_ops1[1].schema_hash = SchemaHash{0x2222};
    auto* body_r1 = crucible::make_region(test.alloc, arena, body_ops1, 2);

    crucible::TraceEntry body_ops2[1]{};
    body_ops2[0].schema_hash = SchemaHash{0x3333};
    auto* body_r2 = crucible::make_region(test.alloc, arena, body_ops2, 1);

    auto* body_term = crucible::make_terminal(test.alloc, arena);
    body_r1->next = body_r2;
    body_r2->next = body_term;

    ContentHash body_ch = crucible::compute_body_content_hash(body_r1);
    assert(static_cast<bool>(body_ch));

    assert(crucible::compute_body_content_hash(body_r1) == body_ch);

    crucible::FeedbackEdge fb_edges[1]{};
    fb_edges[0].output_idx = 0;
    fb_edges[0].input_idx = 0;

    auto* loop =
        crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges, 1, crucible::LoopTermKind::REPEAT, 4);

    assert(loop->kind == crucible::TraceNodeKind::LOOP);
    assert(loop->body == body_r1);
    assert(loop->body_content_hash == body_ch);
    assert(loop->num_feedback == 1);
    assert(loop->feedback_edges[0].output_idx == 0);
    assert(loop->term_kind == crucible::LoopTermKind::REPEAT);
    assert(loop->repeat_count == 4);
    assert(std::bit_cast<uint32_t>(loop->epsilon) == 0);
    static_assert(sizeof(crucible::LoopNode) == 64);
    static_assert(sizeof(crucible::FeedbackEdge) == 4);

    crucible::TraceEntry final_ops[1]{};
    final_ops[0].schema_hash = SchemaHash{0x4444};
    auto* final_region = crucible::make_region(test.alloc, arena, final_ops, 1);
    auto* dag_term = crucible::make_terminal(test.alloc, arena);
    final_region->next = dag_term;
    loop->next = final_region;

    // Merkle hash covers loop body, feedback, termination, and continuation
    crucible::recompute_merkle(loop);
    MerkleHash loop_merkle = loop->merkle_hash;
    assert(static_cast<bool>(loop_merkle));

    auto* loop2 =
        crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges, 1, crucible::LoopTermKind::REPEAT, 8);
    loop2->next = final_region;
    crucible::recompute_merkle(loop2);
    assert(loop2->merkle_hash != loop_merkle);

    auto* loop3 =
        crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges, 1, crucible::LoopTermKind::UNTIL, 4, 0.001f);
    loop3->next = final_region;
    crucible::recompute_merkle(loop3);
    assert(loop3->merkle_hash != loop_merkle);
    assert(loop3->merkle_hash != loop2->merkle_hash);

    crucible::FeedbackEdge fb_edges2[2]{};
    fb_edges2[0] = {.output_idx = 0, .input_idx = 0};
    fb_edges2[1] = {.output_idx = 1, .input_idx = 1};
    auto* loop4 =
        crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges2, 2, crucible::LoopTermKind::REPEAT, 4);
    loop4->next = final_region;
    crucible::recompute_merkle(loop4);
    assert(loop4->merkle_hash != loop_merkle);

    assert(crucible::feedback_signature({}) == 0);
    assert(crucible::feedback_signature(loop->feedback_span()) != 0);

    crucible::RegionNode* collected[16]{};
    uint32_t n_collected = crucible::collect_regions(loop, std::span{collected, 16});
    // Two body regions, counted once, plus the region after the loop.
    assert(n_collected == 3);
    assert(collected[0] == body_r1);
    assert(collected[1] == body_r2);
    assert(collected[2] == final_region);

    uint32_t exec_count = 0;
    std::vector<ContentHash> replay_log;
    replay_log.reserve(16);
    bool replay_ok = crucible::replay(
        loop, [](const crucible::Guard&) -> int64_t { return 0; },
        [&](crucible::RegionNode* r) {
            replay_log.push_back(r->content_hash);
            exec_count++;
        });
    assert(replay_ok);
    // 4 iterations × 2 body regions + 1 continuation region.
    assert(exec_count == 9);
    assert(replay_log.size() == 9);
    for (uint32_t i = 0; i < 8; i += 2) {
        assert(replay_log[i] == body_r1->content_hash);
        assert(replay_log[i + 1] == body_r2->content_hash);
    }
    assert(replay_log[8] == final_region->content_hash);

    auto* loop_zero =
        crucible::make_loop(test.alloc, arena, body_r1, body_ch, fb_edges, 1, crucible::LoopTermKind::REPEAT, 0);
    loop_zero->next = final_region;
    uint32_t zero_count = 0;
    bool zero_ok = crucible::replay(
        loop_zero, [](const crucible::Guard&) -> int64_t { return 0; }, [&](crucible::RegionNode*) { zero_count++; });
    assert(zero_ok);
    assert(zero_count == 1);  // the continuation region alone

    crucible::TraceEntry alt_body_ops[1]{};
    alt_body_ops[0].schema_hash = SchemaHash{0x9999};
    auto* alt_body = crucible::make_region(test.alloc, arena, alt_body_ops, 1);
    auto* alt_body_term = crucible::make_terminal(test.alloc, arena);
    alt_body->next = alt_body_term;
    ContentHash alt_ch = crucible::compute_body_content_hash(alt_body);
    assert(alt_ch != body_ch);

    // Two regions with byte-identical ops but different numerical
    // recipes must produce distinct content hashes.  Otherwise a kernel
    // compiled under one recipe serves lookups made under another, and
    // replay stops being deterministic.
    {
        // hashed() fills the hash field.  compute_content_hash has a
        // precondition that the recipe hash is not zero.
        constexpr crucible::NumericalRecipe recipe_tc = crucible::hashed(crucible::NumericalRecipe{
            .accum_dtype = crucible::ScalarType::Float,
            .out_dtype = crucible::ScalarType::Half,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding = crucible::RoundingMode::RN,
            .scale_policy = crucible::ScalePolicy::NONE,
            .softmax = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism = crucible::ReductionDeterminism::BITEXACT_TC,
            .flags = {},
            .hash = {},
        });
        constexpr crucible::NumericalRecipe recipe_strict = crucible::hashed(crucible::NumericalRecipe{
            .accum_dtype = crucible::ScalarType::Float,
            .out_dtype = crucible::ScalarType::Float,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding = crucible::RoundingMode::RN,
            .scale_policy = crucible::ScalePolicy::NONE,
            .softmax = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism = crucible::ReductionDeterminism::BITEXACT_STRICT,
            .flags = {},
            .hash = {},
        });
        assert(recipe_tc.hash != recipe_strict.hash);
        assert(recipe_tc.hash.raw() != 0);
        assert(!recipe_tc.hash.is_sentinel());
        assert(recipe_strict.hash.raw() != 0);
        assert(!recipe_strict.hash.is_sentinel());

        crucible::TraceEntry recipe_ops[2]{};
        recipe_ops[0].schema_hash = SchemaHash{0xDEAD};
        recipe_ops[1].schema_hash = SchemaHash{0xBEEF};
        const std::span<const crucible::TraceEntry> ops_span{recipe_ops, 2};

        const ContentHash h_default = crucible::compute_content_hash(ops_span);
        const ContentHash h_null = crucible::compute_content_hash(ops_span, nullptr);
        assert(h_default == h_null);

        const ContentHash h_tc = crucible::compute_content_hash(ops_span, &recipe_tc);
        const ContentHash h_strict = crucible::compute_content_hash(ops_span, &recipe_strict);
        assert(h_tc != h_strict);
        assert(h_tc != h_null);
        assert(h_strict != h_null);

        const ContentHash h_tc_again = crucible::compute_content_hash(ops_span, &recipe_tc);
        assert(h_tc == h_tc_again);

        // The recipe contribution is additive: ops still disambiguate
        // under a fixed recipe.
        crucible::TraceEntry alt_recipe_ops[2]{};
        alt_recipe_ops[0].schema_hash = SchemaHash{0xFACE};
        alt_recipe_ops[1].schema_hash = SchemaHash{0xBEEF};
        const std::span<const crucible::TraceEntry> alt_ops_span{alt_recipe_ops, 2};
        const ContentHash h_alt_tc = crucible::compute_content_hash(alt_ops_span, &recipe_tc);
        assert(h_alt_tc != h_tc);

        crucible::TraceEntry rops_a[2]{};
        rops_a[0].schema_hash = SchemaHash{0x0A0A};
        rops_a[1].schema_hash = SchemaHash{0x0B0B};
        crucible::TraceEntry rops_b[2]{};
        rops_b[0].schema_hash = SchemaHash{0x0A0A};
        rops_b[1].schema_hash = SchemaHash{0x0B0B};

        auto* r_tc = crucible::make_region(test.alloc, arena, rops_a, 2, &recipe_tc);
        auto* r_strict = crucible::make_region(test.alloc, arena, rops_b, 2, &recipe_strict);
        auto* r_none = crucible::make_region(test.alloc, arena, rops_a, 2);

        assert(r_tc != nullptr && r_strict != nullptr && r_none != nullptr);
        assert(r_tc->content_hash != r_strict->content_hash);
        assert(r_tc->content_hash != r_none->content_hash);
        assert(r_strict->content_hash != r_none->content_hash);

        const ContentHash direct_tc =
            crucible::compute_content_hash(std::span<const crucible::TraceEntry>{rops_a, 2}, &recipe_tc);
        assert(r_tc->content_hash == direct_tc);

        crucible::KernelCache rcache;
        struct FakeRecipeKernel {
            int tag;
        };
        FakeRecipeKernel tc_kernel{1};
        using crucible::RowHash;
        assert(rcache.insert(r_tc->content_hash, RowHash{0}, kernel_ptr(&tc_kernel)).has_value());
        assert(rcache.lookup(r_tc->content_hash, RowHash{0}) == kernel_ptr(&tc_kernel));
        assert(rcache.lookup(r_strict->content_hash, RowHash{0}) == nullptr);
        assert(rcache.lookup(r_none->content_hash, RowHash{0}) == nullptr);
    }

    // A fold that stops at five scalar arguments, or that ignores the
    // argument count, lets two different ops share a content hash and
    // collide in the kernel cache.  The inputs below pin both axes:
    // every list agrees on its first five entries, so a hash can only
    // separate them by counting the arguments or by reading past index
    // four.
    {
        crucible::TraceEntry e_five{};
        crucible::TraceEntry e_six{};
        crucible::TraceEntry e_six_diff{};
        crucible::TraceEntry e_ten{};

        e_five.schema_hash = SchemaHash{0xAA};
        e_six.schema_hash = SchemaHash{0xAA};
        e_six_diff.schema_hash = SchemaHash{0xAA};
        e_ten.schema_hash = SchemaHash{0xAA};

        int64_t s_five[5] = {1, 2, 3, 4, 5};
        int64_t s_six[6] = {1, 2, 3, 4, 5, 99};
        int64_t s_six_diff[6] = {1, 2, 3, 4, 5, 7};
        int64_t s_ten[10] = {1, 2, 3, 4, 5, 99, 100, 101, 102, 103};

        e_five.scalar_args = s_five;
        e_five.num_scalar_args = 5;
        e_six.scalar_args = s_six;
        e_six.num_scalar_args = 6;
        e_six_diff.scalar_args = s_six_diff;
        e_six_diff.num_scalar_args = 6;
        e_ten.scalar_args = s_ten;
        e_ten.num_scalar_args = 10;

        auto h_five = crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_five, 1});
        auto h_six = crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_six, 1});
        auto h_six_diff = crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_six_diff, 1});
        auto h_ten = crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_ten, 1});

        assert(h_five != h_six);
        assert(h_five != h_ten);
        assert(h_six != h_ten);

        assert(h_six != h_six_diff);

        auto h_six_again = crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_six, 1});
        assert(h_six == h_six_again);

        std::printf("  scalar args: count and past-index-4 value axes both separate\n");
    }

    // The background thread folds a region's content hash one op at a
    // time as it drains the ring, while compute_content_hash folds the
    // same region from a span.  The two must agree bit for bit.
    //
    // The lambda below drives ContentHashFold exactly the way the
    // background thread's build_trace_from does: construct, fold each op
    // as it is populated, finish.  It is the production composition, not
    // a replica of it — a replica would re-spell the seed and the
    // finalizer and so could not witness a change to either.
    //
    // A count == 0 op and an op with no tensors are the two shapes the
    // two folds drift apart on first, so both appear here.
    {
        auto streaming_hash = [](std::span<const crucible::TraceEntry> region_ops) {
            crucible::ContentHashFold fold;
            for (const auto& op : region_ops)
                fold.fold(op);
            return fold.finish();
        };

        crucible::TensorMeta input_meta{};

        crucible::TraceEntry e_empty{};
        e_empty.schema_hash = SchemaHash{0x11};

        crucible::TraceEntry e_count_only{};
        e_count_only.schema_hash = SchemaHash{0x22};
        e_count_only.num_scalar_args = 3;  // scalar_args deliberately stays null

        int64_t s_one[1] = {42};
        crucible::TraceEntry e_one{};
        e_one.schema_hash = SchemaHash{0x33};
        e_one.scalar_args = s_one;
        e_one.num_scalar_args = 1;

        // Five values is the boundary a clamped fold stops at.
        int64_t s_five[5] = {9, 8, 7, 6, 5};
        crucible::TraceEntry e_five{};
        e_five.schema_hash = SchemaHash{0x44};
        e_five.scalar_args = s_five;
        e_five.num_scalar_args = 5;

        crucible::TraceEntry e_tensor{};
        e_tensor.schema_hash = SchemaHash{0x55};
        e_tensor.num_inputs = 1;
        e_tensor.input_metas = &input_meta;

        for (const auto* op : {&e_empty, &e_count_only, &e_one, &e_five, &e_tensor}) {
            std::span<const crucible::TraceEntry> sp{op, 1};
            assert(streaming_hash(sp) == crucible::compute_content_hash(sp));
        }

        // One region of mixed op shapes exercises the accumulator carry
        // from one op shape into the next.
        crucible::TraceEntry mix[] = {e_empty, e_count_only, e_one, e_five, e_tensor};
        std::span<const crucible::TraceEntry> mix_sp{mix, 5};
        assert(streaming_hash(mix_sp) == crucible::compute_content_hash(mix_sp));

        // An op that carries no scalars and no tensors still perturbs the
        // accumulator, so it is not equivalent to an absent op.
        std::span<const crucible::TraceEntry> empty_sp{};
        assert(crucible::compute_content_hash(empty_sp)
               != crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_empty, 1}));

        // The scalar count is folded even when the value pointer is null.
        assert(crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_empty, 1})
               != crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&e_count_only, 1}));

        std::printf("  streaming fold matches the span fold "
                    "(count==0 / no-tensor / scalar / tensor / multi-op)\n");

        // Frozen content-hash vectors.
        //
        // The equivalence above is necessary and no longer sufficient.  The
        // seed, the per-op mix and the finalizer are one object now, so the
        // two producers move together: a change to any of the three keeps
        // them equal to each other and changes what they both produce.
        //
        // These vectors are what makes that change loud.  A region's content
        // hash is the compiler's cache key and is persisted in the Cipher,
        // so moving the format invalidates every stored hash and every
        // cached kernel.  That is a deliberate act, and it updates these
        // numbers in the same commit.
        //
        // The failure path prints what it got, so a deliberate move does not
        // need a separate harness to recover the new values.
        uint32_t moved_vectors = 0;
        auto golden = [&moved_vectors](const char* what, crucible::ContentHash got, uint64_t want) {
            if (got.raw() == want) return;
            ++moved_vectors;
            // Every vector is reported before the abort, so one run recovers
            // the whole replacement set.  stderr, because the abort below
            // discards a block-buffered stdout.
            std::fprintf(stderr,
                         "  content-hash vector '%s' moved: got 0x%016llXULL, "
                         "frozen at 0x%016llXULL\n",
                         what, static_cast<unsigned long long>(got.raw()), static_cast<unsigned long long>(want));
        };

        // The tensor op above carries a default TensorMeta, whose dtype is
        // Undefined.  That is one of the two shapes the fold currently
        // collapses on, so it is a poor thing to pin a format against: the
        // frozen value would be the collapse, not the format.  The vectors
        // use a tensor with a real dtype and device instead, and the
        // collapse gets its own block below.
        crucible::TensorMeta real_meta{};
        real_meta.dtype = crucible::ScalarType::Float;
        real_meta.device_type = crucible::DeviceType::CUDA;
        real_meta.device_idx = 0;

        crucible::TraceEntry e_real_tensor{};
        e_real_tensor.schema_hash = SchemaHash{0x66};
        e_real_tensor.num_inputs = 1;
        e_real_tensor.input_metas = &real_meta;

        crucible::TraceEntry real_mix[] = {e_empty, e_count_only, e_one, e_five, e_real_tensor};
        const std::span<const crucible::TraceEntry> real_mix_sp{real_mix, 5};

        auto one_op = [](const crucible::TraceEntry& op) {
            return crucible::compute_content_hash(std::span<const crucible::TraceEntry>{&op, 1});
        };

        golden("empty region", crucible::compute_content_hash(empty_sp), 0x9CA066F1A4AB2EEAULL);
        golden("no-tensor op", one_op(e_empty), 0xC618BE7298AA9E5EULL);
        golden("scalar count only", one_op(e_count_only), 0xEAA9763A50C958B8ULL);
        golden("one scalar", one_op(e_one), 0x9C8B77BB3E1047B2ULL);
        golden("five scalars", one_op(e_five), 0xC48495C5B66BDFE2ULL);
        golden("one tensor", one_op(e_real_tensor), 0x2E2953936E574B57ULL);
        golden("mixed five-op region", crucible::compute_content_hash(real_mix_sp), 0x6409B52C9172B550ULL);

        // The recipe fold is part of the same format, so it is frozen too.
        constexpr crucible::NumericalRecipe golden_recipe = crucible::hashed(crucible::NumericalRecipe{
            .accum_dtype = crucible::ScalarType::Float,
            .out_dtype = crucible::ScalarType::Half,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding = crucible::RoundingMode::RN,
            .scale_policy = crucible::ScalePolicy::NONE,
            .softmax = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism = crucible::ReductionDeterminism::BITEXACT_TC,
            .flags = {},
            .hash = {},
        });
        golden("mixed five-op region under a recipe", crucible::compute_content_hash(real_mix_sp, &golden_recipe),
               0x63B60C6940443739ULL);

        assert(moved_vectors == 0
               && "the region content-hash format changed: every persisted content hash and "
                  "every cached kernel keyed on one is now stale");

        std::printf("  content-hash vectors frozen "
                    "(seed, per-op mix, finalizer, recipe fold)\n");
    }

    // KNOWN DEFECT, recorded here so a change to the fold has to confront it.
    //
    // The per-tensor step is
    //     state = wymix(state ^ dim_hash, meta_packed)
    // and wymix(a, b) is lo(a*b) ^ hi(a*b), which has two absorbing values
    // for b.  wymix(a, 0) is 0, and wymix(a, ~0) is ~0 for every non-zero a.
    // Reaching either erases the whole region prefix: every op before the
    // tensor stops contributing, and two regions that differ only before
    // that op get the same content hash.
    //
    // Both are reachable from ordinary metadata:
    //
    //   meta_packed == ~0   The pack is
    //                         dtype | device_type << 8 | uint8(device_idx) << 16
    //                       and ScalarType is int8_t with Undefined == -1.
    //                       to_underlying gives int8_t(-1), which
    //                       sign-extends to 0xFFFF'FFFF'FFFF'FFFF and swamps
    //                       the two shifted fields as well.  Any op whose
    //                       first input is an undefined optional tensor
    //                       lands here.  Note the third field already has
    //                       the uint8_t cast the other two are missing.
    //
    //   meta_packed == 0    ScalarType::Byte is 0, DeviceType::CPU is 0, and
    //                       device index 0 is ordinary, so a uint8 tensor on
    //                       the first CPU device packs to zero.  The content
    //                       hash is then 0, which is the KernelCache's
    //                       empty-slot sentinel and what make_region's
    //                       postcondition refuses.
    //
    // The fix changes the hash format, so it invalidates every persisted
    // content hash and every cached kernel, and it belongs in a commit of
    // its own that moves the vectors above with it.  These assertions pin
    // the current behavior so that commit cannot land quietly.
    {
        auto with_meta = [](uint64_t schema, crucible::TensorMeta* meta) {
            crucible::TraceEntry e{};
            e.schema_hash = SchemaHash{schema};
            e.num_inputs = 1;
            e.input_metas = meta;
            return e;
        };
        auto no_tensor = [](uint64_t schema) {
            crucible::TraceEntry e{};
            e.schema_hash = SchemaHash{schema};
            return e;
        };

        crucible::TensorMeta undefined_meta{};  // dtype Undefined == -1
        crucible::TensorMeta zero_packed_meta{};
        zero_packed_meta.dtype = crucible::ScalarType::Byte;
        zero_packed_meta.device_type = crucible::DeviceType::CPU;
        zero_packed_meta.device_idx = 0;

        for (auto* meta : {&undefined_meta, &zero_packed_meta}) {
            crucible::TraceEntry region_a[2] = {no_tensor(0xAAAA), with_meta(0x5555, meta)};
            crucible::TraceEntry region_b[2] = {no_tensor(0xBBBB), with_meta(0x5555, meta)};
            const auto h_a = crucible::compute_content_hash(std::span<const crucible::TraceEntry>{region_a, 2});
            const auto h_b = crucible::compute_content_hash(std::span<const crucible::TraceEntry>{region_b, 2});
            assert(h_a == h_b
                   && "the absorbing-element collapse was fixed: move the frozen vectors above "
                      "and delete this block");
        }

        // The zero-packed case produces the sentinel hash outright.
        crucible::TraceEntry sentinel_region[1] = {with_meta(0x5555, &zero_packed_meta)};
        assert(crucible::compute_content_hash(std::span<const crucible::TraceEntry>{sentinel_region, 1}).raw() == 0
               && "the zero-pack collapse was fixed: move the frozen vectors above and delete this block");

        std::printf("  known absorbing-element collapse pinned "
                    "(dtype Undefined and zero-packed metadata)\n");
    }

    std::printf("test_merkle_dag: all tests passed\n");
    return 0;
}
