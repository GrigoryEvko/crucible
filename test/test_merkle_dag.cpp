#include <crucible/KernelCache.h>
#include <crucible/MerkleDag.h>
#include <fixy/concurrent/HandleTraits.h>
#include <foundation/Linear.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "test_assert.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

using crucible::SchemaHash;
using crucible::ContentHash;
using crucible::MerkleHash;

namespace {

// The cache never reads a kernel, so the address of a fake object stands
// for one.
template <class Fake>
[[nodiscard]] crucible::KernelCache::KernelRef kernel_ref(Fake& fake) noexcept {
    return ::fixy::to(*static_cast<crucible::CompiledKernel*>(static_cast<void*>(&fake)));
}

// True when the lookup found the kernel that the fake object stands for.
template <class Fake>
[[nodiscard]] bool is_found(crucible::KernelCache::FoundKernel const& found, Fake& fake) noexcept {
    return found.match(
        [&fake](crucible::KernelCache::KernelRef const& kernel) noexcept {
            return ::fixy::same_object(kernel, kernel_ref(fake));
        },
        []() noexcept { return false; });
}

// The content hash of a region of one op, under no recipe.
[[nodiscard]] crucible::ContentHash hash_one_op(const crucible::TraceEntry& op) noexcept {
    const ::foundation::core::FixedArray<crucible::TraceEntry, 1> single{op};
    return crucible::compute_content_hash(single.view());
}

}  // namespace

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena(1 << 16);

    crucible::TensorMeta m{};
    m.ndim = 2;
    m.sizes[0] = ::crucible::tensor_dim(32);
    m.sizes[1] = ::crucible::tensor_dim(64);
    m.strides[0] = ::crucible::tensor_dim(64);
    m.strides[1] = ::crucible::tensor_dim(1);
    m.dtype = crucible::ScalarType::Float;
    auto nbytes = crucible::compute_storage_nbytes(crucible::external_tensor_meta(m));
    auto nbytes_det = crucible::compute_storage_nbytes_det(crucible::external_tensor_meta(m));
    static_assert(std::is_same_v<decltype(nbytes_det),
                                 ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, ::fixy::Saturated<uint64_t>>>);
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

    // The public name and the vector routine are one algorithm.
    //
    // This assertion could not say anything before: compute_storage_nbytes
    // was a separate transcription of the same steps in MerkleDag.h, so
    // comparing it against the vector routine compared two independent
    // copies that happened to agree, and the differential fuzzer next door
    // was pinning the two copies production did not use. The public name
    // forwards to the scalar reference now, which is the routine the fuzzer
    // holds the vector one against.
    //
    // The shapes below are the ones the two paths diverge on first: a
    // negative stride, which drives the min/max accumulators apart, a zero
    // extent, which returns before the vector path's screen, and a scalar,
    // which returns before either loop.
    {
        auto agrees = [](const crucible::TensorMeta& meta) {
            return crucible::compute_storage_nbytes(crucible::external_tensor_meta(meta))
                == crucible::detail::compute_storage_nbytes_simd(crucible::external_tensor_meta(meta));
        };

        crucible::TensorMeta reversed{};
        reversed.ndim = 2;
        reversed.sizes[0] = ::crucible::tensor_dim(8);
        reversed.sizes[1] = ::crucible::tensor_dim(4);
        reversed.strides[0] = ::crucible::tensor_dim(-4);
        reversed.strides[1] = ::crucible::tensor_dim(1);
        reversed.dtype = crucible::ScalarType::Double;
        assert(agrees(reversed) && "the public storage-span name stopped forwarding to the tested reference");

        crucible::TensorMeta empty_extent = reversed;
        empty_extent.sizes[1] = ::crucible::tensor_dim(0);
        assert(agrees(empty_extent));

        crucible::TensorMeta scalar_meta{};
        scalar_meta.ndim = 0;
        scalar_meta.dtype = crucible::ScalarType::ComplexDouble;
        assert(agrees(scalar_meta));

        assert(agrees(m));
    }

    ::foundation::core::FixedArray<crucible::TraceEntry, 3> ops{};
    ops.at(::fixy::index<0>()).schema_hash = SchemaHash{0xAABB};
    ops.at(::fixy::index<1>()).schema_hash = SchemaHash{0xCCDD};
    ops.at(::fixy::index<2>()).schema_hash = SchemaHash{0xEEFF};
    // The nodes are held as references: a Ref local in this large function
    // costs the Debug compile far more than a reference.
    crucible::RegionNode& region = *crucible::make_region(test.alloc, arena, ops.view());
    assert(region.kind == crucible::TraceNodeKind::REGION);
    assert(region.num_ops() == 3);
    assert(region.op_at(0).schema_hash == SchemaHash{0xAABB});
    assert(region.op_at(2).schema_hash == SchemaHash{0xEEFF});
    assert(region.first_op_schema == SchemaHash{0xAABB});
    assert(static_cast<bool>(region.content_hash));

    ContentHash h1 = region.content_hash;
    const crucible::RegionNode& region2 = *crucible::make_region(test.alloc, arena, ops.view());
    assert(region2.content_hash == h1);

    crucible::TraceNode& terminal = *crucible::make_terminal(test.alloc, arena);
    assert(terminal.kind == crucible::TraceNodeKind::TERMINAL);
    assert(terminal.next.is_none());

    region.next = crucible::make_link(terminal);
    crucible::recompute_merkle(region);
    assert(static_cast<bool>(region.merkle_hash));

    // RowHash{0} is the row identity of a bare, unwrapped type, so it is
    // the row every untyped call site keys on.
    crucible::KernelCache cache{test.alloc};
    using crucible::RowHash;
    using KernelSlot = crucible::KernelCache::KernelCacheSlot;
    using SlotSnapshot = crucible::KernelCache::KernelCacheSlotSnapshot;
    static_assert(sizeof(KernelSlot) == 24);
    static_assert(alignof(KernelSlot) == 8);
    static_assert(sizeof(SlotSnapshot) == 24);
    static_assert(::fixy::concurrent::IsSwmrWriter<KernelSlot::WriterHandle>);
    static_assert(::foundation::linear::is_marked_linear(^^KernelSlot::WriterHandle),
                  "the writer handle holds a linear token, and release() is its door");
    static_assert(::fixy::concurrent::IsSwmrReader<KernelSlot::ReaderHandle>);
    static_assert(std::is_same_v<::fixy::concurrent::swmr_writer_value_t<KernelSlot::WriterHandle>, SlotSnapshot>);
    static_assert(std::is_same_v<::fixy::concurrent::swmr_reader_value_t<KernelSlot::ReaderHandle>, SlotSnapshot>);

    CRUCIBLE_FATAL_INVARIANT(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0}).is_none());
    struct FakeKernel {
        int x;
    };
    FakeKernel fk{42};
    CRUCIBLE_FATAL_INVARIANT(cache.insert(test.nondet, ContentHash{0x1234}, RowHash{0}, kernel_ref(fk)).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0}), fk));
    FakeKernel fk2{99};
    CRUCIBLE_FATAL_INVARIANT(cache.insert(test.nondet, ContentHash{0x1234}, RowHash{0}, kernel_ref(fk2)).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0}), fk2));

    FakeKernel fk_row{777};
    CRUCIBLE_FATAL_INVARIANT(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0xAA}).is_none());
    CRUCIBLE_FATAL_INVARIANT(cache.insert(test.nondet, ContentHash{0x1234}, RowHash{0xAA}, kernel_ref(fk_row)).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0xAA}), fk_row));
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0}), fk2));
    CRUCIBLE_FATAL_INVARIANT(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0xBB}).is_none());

    // The probe distinguishes a variant update from a sibling row: an
    // insert under an occupied (content, row) replaces that slot alone.
    FakeKernel fk_row_v2{888};
    CRUCIBLE_FATAL_INVARIANT(
        cache.insert(test.nondet, ContentHash{0x1234}, RowHash{0xAA}, kernel_ref(fk_row_v2)).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0xAA}), fk_row_v2));
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0}), fk2));

    // Row discrimination is not bound to one content_hash slot index.
    FakeKernel fk_other{555};
    CRUCIBLE_FATAL_INVARIANT(
        cache.insert(test.nondet, ContentHash{0x9999}, RowHash{0xAA}, kernel_ref(fk_other)).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x9999}, RowHash{0xAA}), fk_other));
    CRUCIBLE_FATAL_INVARIANT(cache.lookup(test.nondet, ContentHash{0x9999}, RowHash{0}).is_none());
    CRUCIBLE_FATAL_INVARIANT(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0xCC}).is_none());

    // The sentinel row is reserved for end-of-region markers elsewhere.
    // The cache must treat it as an ordinary 64-bit row identity.
    FakeKernel fk_sentinel{0xFEED};
    CRUCIBLE_FATAL_INVARIANT(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash::sentinel()).is_none());
    CRUCIBLE_FATAL_INVARIANT(
        cache.insert(test.nondet, ContentHash{0x1234}, RowHash::sentinel(), kernel_ref(fk_sentinel)).is_ok());
    CRUCIBLE_FATAL_INVARIANT(
        is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash::sentinel()), fk_sentinel));
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0}), fk2));
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup(test.nondet, ContentHash{0x1234}, RowHash{0xAA}), fk_row_v2));

    // Many rows under one content_hash drive the probe chain around
    // the table.
    {
        crucible::KernelCache row_pressure_cache(test.alloc, /*capacity=*/16);
        constexpr uint32_t N = 8;  // half the table — leaves room for probes
        FakeKernel row_kernels[N];
        for (uint32_t i = 0; i < N; ++i) {
            row_kernels[i] = FakeKernel{static_cast<int>(0x1000 + i)};
            CRUCIBLE_FATAL_INVARIANT(row_pressure_cache
                                         .insert(test.nondet, ContentHash{0xABCDEF},  // one shared content
                                                 RowHash{0x100 + i},  // distinct rows
                                                 kernel_ref(row_kernels[i]))
                                         .is_ok());
        }
        for (uint32_t i = 0; i < N; ++i) {
            CRUCIBLE_FATAL_INVARIANT(is_found(
                row_pressure_cache.lookup(test.nondet, ContentHash{0xABCDEF}, RowHash{0x100 + i}), row_kernels[i]));
        }
        // The probe terminates on an absent row instead of matching a
        // foreign-row sibling that shares the content hash.
        CRUCIBLE_FATAL_INVARIANT(
            row_pressure_cache.lookup(test.nondet, ContentHash{0xABCDEF}, RowHash{0xDEAD}).is_none());
    }

    {
        crucible::KernelCache tiny_cache(test.alloc, /*capacity=*/4);
        FakeKernel small_kernels[5];
        for (uint32_t i = 0; i < 4; ++i) {
            small_kernels[i] = FakeKernel{static_cast<int>(i)};
            CRUCIBLE_FATAL_INVARIANT(
                tiny_cache.insert(test.nondet, ContentHash{0x42}, RowHash{0xA00 + i}, kernel_ref(small_kernels[i]))
                    .is_ok());
        }
        // The fifth insert finds no empty slot: all four hold the same
        // content under a different row, so none is a variant update.
        small_kernels[4] = FakeKernel{4};
        auto fifth = tiny_cache.insert(test.nondet, ContentHash{0x42}, RowHash{0xA04},  // novel row
                                       kernel_ref(small_kernels[4]));
        assert(std::move(fifth).err().match(
            [](crucible::KernelCache::InsertError error) noexcept {
                return error == crucible::KernelCache::InsertError::TableFull;
            },
            []() noexcept { return false; }));
        // A full table still accepts a variant update.  TableFull is
        // returned only when no (content, row) match exists.
        FakeKernel variant_replacement{0xC};
        auto variant = tiny_cache.insert(test.nondet, ContentHash{0x42}, RowHash{0xA00},  // existing row
                                         kernel_ref(variant_replacement));
        assert(variant.is_ok());
        CRUCIBLE_FATAL_INVARIANT(
            is_found(tiny_cache.lookup(test.nondet, ContentHash{0x42}, RowHash{0xA00}), variant_replacement));
    }

    // RowHash{0} is a lookup target like any other row.  This sequence
    // is the single-threaded stand-in for a concurrent hazard: an
    // inserter that observes a claimed-but-unpublished slot for content
    // C must not mistake it for the (C, 0) slot and silently lose the
    // pending kernel.  Inserting under row 0, then a second row, then
    // row 0 again reproduces the same slot observation the race makes.
    {
        crucible::KernelCache audit2_cache(test.alloc, /*capacity=*/16);
        FakeKernel k_row0_v1{1};
        FakeKernel k_rowR_v1{2};
        FakeKernel k_row0_v2{3};

        CRUCIBLE_FATAL_INVARIANT(
            audit2_cache.insert(test.nondet, ContentHash{0x55AA}, RowHash{0}, kernel_ref(k_row0_v1)).is_ok());

        CRUCIBLE_FATAL_INVARIANT(
            audit2_cache.insert(test.nondet, ContentHash{0x55AA}, RowHash{0xBEEF}, kernel_ref(k_rowR_v1)).is_ok());

        CRUCIBLE_FATAL_INVARIANT(
            is_found(audit2_cache.lookup(test.nondet, ContentHash{0x55AA}, RowHash{0}), k_row0_v1));
        CRUCIBLE_FATAL_INVARIANT(
            is_found(audit2_cache.lookup(test.nondet, ContentHash{0x55AA}, RowHash{0xBEEF}), k_rowR_v1));

        CRUCIBLE_FATAL_INVARIANT(
            audit2_cache.insert(test.nondet, ContentHash{0x55AA}, RowHash{0}, kernel_ref(k_row0_v2)).is_ok());

        CRUCIBLE_FATAL_INVARIANT(
            is_found(audit2_cache.lookup(test.nondet, ContentHash{0x55AA}, RowHash{0}), k_row0_v2));
        CRUCIBLE_FATAL_INVARIANT(
            is_found(audit2_cache.lookup(test.nondet, ContentHash{0x55AA}, RowHash{0xBEEF}), k_rowR_v1));
    }

    assert(crucible::element_size(crucible::ScalarType::Float) == crucible::ElementBytes{4});
    assert(crucible::element_size(crucible::ScalarType::Double) == crucible::ElementBytes{8});
    assert(crucible::element_size(crucible::ScalarType::Half) == crucible::ElementBytes{2});
    assert(crucible::element_size(crucible::ScalarType::Byte) == crucible::ElementBytes{1});
    assert(crucible::element_size(crucible::ScalarType::ComplexDouble) == crucible::ElementBytes{16});

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
            .rounding = ::foundation::core::RoundingMode::RN,
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
            .rounding = ::foundation::core::RoundingMode::RN,
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

        ::foundation::core::FixedArray<crucible::TraceEntry, 2> recipe_ops{};
        recipe_ops.at(::fixy::index<0>()).schema_hash = SchemaHash{0xDEAD};
        recipe_ops.at(::fixy::index<1>()).schema_hash = SchemaHash{0xBEEF};
        const ::fixy::View<const crucible::TraceEntry> ops_span = recipe_ops.view();

        const ContentHash h_null = crucible::compute_content_hash(ops_span);

        const ContentHash h_tc = crucible::compute_content_hash(ops_span, recipe_tc);
        const ContentHash h_strict = crucible::compute_content_hash(ops_span, recipe_strict);
        assert(h_tc != h_strict);
        assert(h_tc != h_null);
        assert(h_strict != h_null);

        const ContentHash h_tc_again = crucible::compute_content_hash(ops_span, recipe_tc);
        assert(h_tc == h_tc_again);

        // The recipe contribution is additive: ops still disambiguate
        // under a fixed recipe.
        ::foundation::core::FixedArray<crucible::TraceEntry, 2> alt_recipe_ops{};
        alt_recipe_ops.at(::fixy::index<0>()).schema_hash = SchemaHash{0xFACE};
        alt_recipe_ops.at(::fixy::index<1>()).schema_hash = SchemaHash{0xBEEF};
        const ::fixy::View<const crucible::TraceEntry> alt_ops_span = alt_recipe_ops.view();
        const ContentHash h_alt_tc = crucible::compute_content_hash(alt_ops_span, recipe_tc);
        assert(h_alt_tc != h_tc);

        ::foundation::core::FixedArray<crucible::TraceEntry, 2> rops_a{};
        rops_a.at(::fixy::index<0>()).schema_hash = SchemaHash{0x0A0A};
        rops_a.at(::fixy::index<1>()).schema_hash = SchemaHash{0x0B0B};
        ::foundation::core::FixedArray<crucible::TraceEntry, 2> rops_b{};
        rops_b.at(::fixy::index<0>()).schema_hash = SchemaHash{0x0A0A};
        rops_b.at(::fixy::index<1>()).schema_hash = SchemaHash{0x0B0B};

        const crucible::RegionNode& r_tc = *crucible::make_region(test.alloc, arena, rops_a.view(), recipe_tc);
        const crucible::RegionNode& r_strict = *crucible::make_region(test.alloc, arena, rops_b.view(), recipe_strict);
        const crucible::RegionNode& r_none = *crucible::make_region(test.alloc, arena, rops_a.view());

        assert(r_tc.content_hash != r_strict.content_hash);
        assert(r_tc.content_hash != r_none.content_hash);
        assert(r_strict.content_hash != r_none.content_hash);

        const ContentHash direct_tc = crucible::compute_content_hash(rops_a.view(), recipe_tc);
        assert(r_tc.content_hash == direct_tc);

        crucible::KernelCache rcache{test.alloc};
        struct FakeRecipeKernel {
            int tag;
        };
        FakeRecipeKernel tc_kernel{1};
        using crucible::RowHash;
        CRUCIBLE_FATAL_INVARIANT(
            rcache.insert(test.nondet, r_tc.content_hash, RowHash{0}, kernel_ref(tc_kernel)).is_ok());
        CRUCIBLE_FATAL_INVARIANT(is_found(rcache.lookup(test.nondet, r_tc.content_hash, RowHash{0}), tc_kernel));
        CRUCIBLE_FATAL_INVARIANT(rcache.lookup(test.nondet, r_strict.content_hash, RowHash{0}).is_none());
        CRUCIBLE_FATAL_INVARIANT(rcache.lookup(test.nondet, r_none.content_hash, RowHash{0}).is_none());
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

        ::foundation::core::FixedArray<int64_t, 5> s_five{1, 2, 3, 4, 5};
        ::foundation::core::FixedArray<int64_t, 6> s_six{1, 2, 3, 4, 5, 99};
        ::foundation::core::FixedArray<int64_t, 6> s_six_diff{1, 2, 3, 4, 5, 7};
        ::foundation::core::FixedArray<int64_t, 10> s_ten{1, 2, 3, 4, 5, 99, 100, 101, 102, 103};

        e_five.set_scalar_args(s_five.view());
        e_six.set_scalar_args(s_six.view());
        e_six_diff.set_scalar_args(s_six_diff.view());
        e_ten.set_scalar_args(s_ten.view());

        auto h_five = hash_one_op(e_five);
        auto h_six = hash_one_op(e_six);
        auto h_six_diff = hash_one_op(e_six_diff);
        auto h_ten = hash_one_op(e_ten);

        assert(h_five != h_six);
        assert(h_five != h_ten);
        assert(h_six != h_ten);

        assert(h_six != h_six_diff);

        auto h_six_again = hash_one_op(e_six);
        assert(h_six == h_six_again);

        ::fixy::report(::fixy::Sink::Out, "  scalar args: count and past-index-4 value axes both separate\n");
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
        auto streaming_hash = [](::fixy::View<const crucible::TraceEntry> region_ops) {
            crucible::ContentHashFold fold{crucible::ContentHashFold::NoRecipe{}};
            for (const auto& op : region_ops)
                fold.fold(op);
            return fold.finish();
        };

        ::foundation::core::FixedArray<crucible::TensorMeta, 1> input_meta{};
        // The fold reads no producer and no slot id of an input.
        ::foundation::core::FixedArray<crucible::OpIndex, 1> no_producer{};
        ::foundation::core::FixedArray<crucible::SlotId, 1> no_slot{};

        crucible::TraceEntry e_empty{};
        e_empty.schema_hash = SchemaHash{0x11};

        ::foundation::core::FixedArray<int64_t, 3> s_three{7, 8, 9};
        crucible::TraceEntry e_three{};
        e_three.schema_hash = SchemaHash{0x22};
        e_three.set_scalar_args(s_three.view());

        ::foundation::core::FixedArray<int64_t, 1> s_one{42};
        crucible::TraceEntry e_one{};
        e_one.schema_hash = SchemaHash{0x33};
        e_one.set_scalar_args(s_one.view());

        // Five values is the boundary a clamped fold stops at.
        ::foundation::core::FixedArray<int64_t, 5> s_five{9, 8, 7, 6, 5};
        crucible::TraceEntry e_five{};
        e_five.schema_hash = SchemaHash{0x44};
        e_five.set_scalar_args(s_five.view());

        crucible::TraceEntry e_tensor{};
        e_tensor.schema_hash = SchemaHash{0x55};
        e_tensor.set_inputs(input_meta.view(), no_producer.view(), no_slot.view());

        for (const auto* op : {&e_empty, &e_three, &e_one, &e_five, &e_tensor}) {
            const ::foundation::core::FixedArray<crucible::TraceEntry, 1> single{*op};
            assert(streaming_hash(single.view()) == crucible::compute_content_hash(single.view()));
        }

        // One region of mixed op shapes exercises the accumulator carry
        // from one op shape into the next.
        const ::foundation::core::FixedArray<crucible::TraceEntry, 5> mix{e_empty, e_three, e_one, e_five, e_tensor};
        const ::fixy::View<const crucible::TraceEntry> mix_sp = mix.view();
        assert(streaming_hash(mix_sp) == crucible::compute_content_hash(mix_sp));

        // An op that carries no scalars and no tensors still perturbs the
        // accumulator, so it is not equivalent to an absent op.
        const ::fixy::View<const crucible::TraceEntry> empty_sp{};
        assert(crucible::compute_content_hash(empty_sp) != hash_one_op(e_empty));

        ::fixy::report(::fixy::Sink::Out, "  streaming fold matches the span fold "
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
        // Undefined.  That was one of the two shapes the old wymix fold
        // collapsed on, so pinning a format against it would have frozen the
        // collapse rather than the format.  The fold is a fmix64 chain now
        // and neither shape is special, but the vectors keep using a tensor
        // with a real dtype and device: a vector should exercise the ordinary
        // path, and the two former collapse shapes have their own block below.
        ::foundation::core::FixedArray<crucible::TensorMeta, 1> real_meta{};
        crucible::TensorMeta& real_input = real_meta.at(::foundation::core::index<0>());
        real_input.dtype = crucible::ScalarType::Float;
        real_input.device_type = crucible::DeviceType::CUDA;
        real_input.device_idx = 0;

        crucible::TraceEntry e_real_tensor{};
        e_real_tensor.schema_hash = SchemaHash{0x66};
        e_real_tensor.set_inputs(real_meta.view(), no_producer.view(), no_slot.view());

        const ::foundation::core::FixedArray<crucible::TraceEntry, 5> real_mix{e_empty, e_three, e_one, e_five,
                                                                               e_real_tensor};
        const ::fixy::View<const crucible::TraceEntry> real_mix_sp = real_mix.view();

        auto one_op = [](const crucible::TraceEntry& op) { return hash_one_op(op); };

        // Moved 2026-09-15 when the fold became a single fmix64 chain.  The
        // empty-region vector is unchanged: an empty region never enters
        // fold_entry_, so it is the seed through the finalizer and nothing else.
        // Moved 2026-11-16 when each op folds the tags of its scalars, in one
        // more step of the chain.  Moved again the same day when each op
        // folds its depth, its tensor counts, the producer of each input and
        // the metadata of each output, and no slot id.
        golden("empty region", crucible::compute_content_hash(empty_sp), 0x9CA066F1A4AB2EEAULL);
        golden("no-tensor op", one_op(e_empty), 0x611B71992AFE4595ULL);
        golden("one scalar", one_op(e_one), 0xBAD217C444EEC11EULL);
        golden("five scalars", one_op(e_five), 0xA3A7C6CC6E781A33ULL);
        golden("one tensor", one_op(e_real_tensor), 0x64DA6CCE7287AAF1ULL);
        // The region holds three present scalars as its second op.  An entry
        // holds each of its arrays, so the fold has no count-only op.
        golden("mixed five-op region", crucible::compute_content_hash(real_mix_sp), 0x954F8A32BC3C16B1ULL);

        // The recipe fold is part of the same format, so it is frozen too.
        constexpr crucible::NumericalRecipe golden_recipe = crucible::hashed(crucible::NumericalRecipe{
            .accum_dtype = crucible::ScalarType::Float,
            .out_dtype = crucible::ScalarType::Half,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding = ::foundation::core::RoundingMode::RN,
            .scale_policy = crucible::ScalePolicy::NONE,
            .softmax = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism = crucible::ReductionDeterminism::BITEXACT_TC,
            .flags = {},
            .hash = {},
        });
        golden("mixed five-op region under a recipe", crucible::compute_content_hash(real_mix_sp, golden_recipe),
               0x89CDAB5A7F0898C6ULL);

        // A tensor with each fact that the word of a tensor folds beside its
        // dims, its element type and its device: a rank of two, the sparse
        // layout, the negative and conjugate bits and a storage offset.
        // Frozen 2026-11-16 when the word took these facts.  The tensor of
        // the vectors above has the rank zero, the strided layout, no bit and
        // the offset zero, so its word did not move.
        ::foundation::core::FixedArray<crucible::TensorMeta, 1> fact_meta{real_input};
        crucible::TensorMeta& fact_input = fact_meta.at(::foundation::core::index<0>());
        fact_input.ndim = 2;
        fact_input.sizes[0] = crucible::tensor_dim(3);
        fact_input.sizes[1] = crucible::tensor_dim(4);
        fact_input.strides[0] = crucible::tensor_dim(4);
        fact_input.strides[1] = crucible::tensor_dim(1);
        fact_input.layout = crucible::Layout::Sparse;
        fact_input.flags = static_cast<uint8_t>(crucible::meta_flags::IS_NEG | crucible::meta_flags::IS_CONJ);
        fact_input.storage_offset = 5;
        crucible::TraceEntry e_fact_tensor{};
        e_fact_tensor.schema_hash = SchemaHash{0x66};
        e_fact_tensor.set_inputs(fact_meta.view(), no_producer.view(), no_slot.view());
        golden("one tensor with each fact", one_op(e_fact_tensor), 0x2FB41C5109940EECULL);

        assert(moved_vectors == 0
               && "the region content-hash format changed: every persisted content hash and "
                  "every cached kernel keyed on one is now stale");

        ::fixy::report(::fixy::Sink::Out, "  content-hash vectors frozen "
                                          "(seed, per-op mix, finalizer, recipe fold)\n");
    }

    // REGRESSION PIN for the absorbing-element collapse, fixed 2026-09-15.
    //
    // The per-tensor step used to be
    //     state = wymix(state ^ dim_hash, meta_packed)
    // and wymix(a, b) is lo(a*b) ^ hi(a*b), which has two absorbing values
    // for b.  wymix(a, 0) is 0, and wymix(a, ~0) is ~0 for every non-zero a.
    // Reaching either erased the whole region prefix: every op before the
    // tensor stopped contributing, and two regions differing only before that
    // op got the same content hash — on the value that keys the compiler's
    // cache.
    //
    // Both were reachable from ordinary metadata, which is why these two
    // metas and not some contrived pair:
    //
    //   meta_packed == ~0   The pack is
    //                         dtype | device_type << 8 | uint8(device_idx) << 16
    //                       and ScalarType is int8_t with Undefined == -1.
    //                       to_underlying gives int8_t(-1), which
    //                       sign-extends to 0xFFFF'FFFF'FFFF'FFFF and swamps
    //                       the two shifted fields as well.  Any op whose
    //                       first input is an undefined optional tensor
    //                       landed here.  Note the third field already has
    //                       the uint8_t cast the other two are missing.
    //
    //   meta_packed == 0    ScalarType::Byte is 0, DeviceType::CPU is 0, and
    //                       device index 0 is ordinary, so a uint8 tensor on
    //                       the first CPU device packs to zero.  The content
    //                       hash was then 0, which is the KernelCache's
    //                       empty-slot sentinel and what make_region's
    //                       postcondition refuses.
    //
    // The fold is a chain of fmix64 now.  fmix64 is a permutation of the
    // 64-bit words, so no input can erase the accumulator and neither meta is
    // special any more.  These assertions are the other side of the ones that
    // used to pin the defect: same two metas, opposite expectation.
    {
        // The fold reads no producer and no slot id of an input.
        ::foundation::core::FixedArray<crucible::OpIndex, 1> no_producer{};
        ::foundation::core::FixedArray<crucible::SlotId, 1> no_slot{};
        auto with_meta = [&no_producer, &no_slot](uint64_t schema, ::fixy::View<crucible::TensorMeta> meta) {
            crucible::TraceEntry e{};
            e.schema_hash = SchemaHash{schema};
            e.set_inputs(meta, no_producer.view(), no_slot.view());
            return e;
        };
        auto no_tensor = [](uint64_t schema) {
            crucible::TraceEntry e{};
            e.schema_hash = SchemaHash{schema};
            return e;
        };

        ::foundation::core::FixedArray<crucible::TensorMeta, 1> undefined_meta{};  // dtype Undefined == -1
        ::foundation::core::FixedArray<crucible::TensorMeta, 1> zero_packed_meta{};
        crucible::TensorMeta& zero_packed = zero_packed_meta.at(::foundation::core::index<0>());
        zero_packed.dtype = crucible::ScalarType::Byte;
        zero_packed.device_type = crucible::DeviceType::CPU;
        zero_packed.device_idx = 0;

        for (const ::fixy::View<crucible::TensorMeta> meta : {undefined_meta.view(), zero_packed_meta.view()}) {
            const ::foundation::core::FixedArray<crucible::TraceEntry, 2> region_a{no_tensor(0xAAAA),
                                                                                   with_meta(0x5555, meta)};
            const ::foundation::core::FixedArray<crucible::TraceEntry, 2> region_b{no_tensor(0xBBBB),
                                                                                   with_meta(0x5555, meta)};
            const auto h_a = crucible::compute_content_hash(region_a.view());
            const auto h_b = crucible::compute_content_hash(region_b.view());
            assert(h_a != h_b
                   && "absorbing-element collapse is back: two regions differing only in the op "
                      "BEFORE the tensor now share a content hash, so the prefix stopped "
                      "contributing");
        }

        // The zero-packed case used to produce the sentinel hash outright.
        assert(hash_one_op(with_meta(0x5555, zero_packed_meta.view())).raw() != 0
               && "a uint8 tensor on CPU:0 hashes to the KernelCache empty-slot sentinel again");

        // A single mix is xor-symmetric, so a fold flattened into one xor
        // would stop telling op order apart.  The chain is what carries it.
        const ::foundation::core::FixedArray<crucible::TraceEntry, 2> forward{no_tensor(0x1111), no_tensor(0x2222)};
        const ::foundation::core::FixedArray<crucible::TraceEntry, 2> reversed{no_tensor(0x2222), no_tensor(0x1111)};
        assert(crucible::compute_content_hash(forward.view()) != crucible::compute_content_hash(reversed.view())
               && "the fold lost op-order sensitivity: it was flattened into a single xor");

        ::fixy::report(::fixy::Sink::Out, "  absorbing-element collapse repaired and pinned "
                                          "(dtype Undefined, zero-packed metadata, op order)\n");
    }

    // ── A recipe-blind content hash collides across numerics ──────────
    //
    // The content hash is the compiler's cache key. Two regions with
    // byte-identical ops under different ReductionDeterminism tiers are
    // different computations and must not share a slot: a kernel compiled
    // under BITEXACT_STRICT answering a lookup made under UNORDERED is a
    // wrong answer, not a slow one.
    //
    // The first assertion is the collision itself, and it is not a bug to be
    // fixed -- it is what a hash that omits the recipe necessarily does, and
    // it is here so the cost of omitting one is written down. The second is
    // the repair: fold the recipe and the two separate. The third is the part
    // that had gone wrong in practice, and it is a compile-time property
    // rather than a runtime one, so it is asserted where it lives -- at
    // ContentHashFold's deleted default constructor, which is what stops a
    // producer from reaching the first case by saying nothing at all.
    {
        ::foundation::core::FixedArray<crucible::TraceEntry, 2> numerics_ops{};
        numerics_ops.at(::fixy::index<0>()).schema_hash = SchemaHash{0xAC01};
        numerics_ops.at(::fixy::index<1>()).schema_hash = SchemaHash{0xAC02};
        const ::fixy::View<const crucible::TraceEntry> numerics_sp = numerics_ops.view();

        constexpr auto unordered = crucible::hashed(crucible::NumericalRecipe{
            .accum_dtype = crucible::ScalarType::Float,
            .out_dtype = crucible::ScalarType::Float,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding = ::foundation::core::RoundingMode::RN,
            .scale_policy = crucible::ScalePolicy::NONE,
            .softmax = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism = crucible::ReductionDeterminism::UNORDERED,
            .flags = {},
            .hash = {},
        });
        constexpr auto strict = crucible::hashed(crucible::NumericalRecipe{
            .accum_dtype = crucible::ScalarType::Float,
            .out_dtype = crucible::ScalarType::Float,
            .reduction_algo = crucible::ReductionAlgo::PAIRWISE,
            .rounding = ::foundation::core::RoundingMode::RN,
            .scale_policy = crucible::ScalePolicy::NONE,
            .softmax = crucible::SoftmaxRecurrence::ONLINE_LSE,
            .determinism = crucible::ReductionDeterminism::BITEXACT_STRICT,
            .flags = {},
            .hash = {},
        });
        static_assert(unordered.hash != strict.hash, "two determinism tiers must intern to different recipe hashes");

        // What a recipe-blind producer computes: one key for both tiers.
        const auto blind = crucible::compute_content_hash(numerics_sp);
        assert(blind == crucible::compute_content_hash(numerics_sp)
               && "a recipe-blind hash is a function of the ops alone, so both tiers land on it");

        // What a recipe-aware producer computes: one key per tier.
        const auto keyed_unordered = crucible::compute_content_hash(numerics_sp, unordered);
        const auto keyed_strict = crucible::compute_content_hash(numerics_sp, strict);
        assert(keyed_unordered != keyed_strict
               && "the recipe left the content hash: two numerics tiers over identical ops "
                  "now share a KernelCache slot, so a lookup under one is served the other's kernel");
        assert(keyed_unordered != blind && keyed_strict != blind
               && "folding a recipe must move the hash off the no-recipe value, or the "
                  "recipe-aware and recipe-blind keys alias");

        // The hole that let production reach the first case: a fold that
        // could be built without answering the recipe question at all.
        static_assert(!std::is_default_constructible_v<crucible::ContentHashFold>,
                      "ContentHashFold is default-constructible again: a producer can build a "
                      "recipe-blind cache key by omission, which is how the runtime path got one");
        static_assert(std::is_constructible_v<crucible::ContentHashFold, crucible::ContentHashFold::NoRecipe>);
        static_assert(std::is_constructible_v<crucible::ContentHashFold, const crucible::NumericalRecipe&>);

        ::fixy::report(::fixy::Sink::Out,
                       "  content hash separates numerics tiers; a fold cannot omit the recipe question\n");
    }

    // ── make_loop's preconditions actually fire ───────────────────────
    //
    // These are runtime checks and handle_contract_violation is [[noreturn]],
    // so a violation cannot be observed in-process.  Each refused call runs
    // in a child through expect_dies of the Check family, which requires
    // SIGABRT and the violated condition on the standard error of the child.
    // A well-formed loop runs in the test process.
    {
        ::fixy::TestRunnerCtx const runner{::foundation::effects::testing::test()};

        ::foundation::core::FixedArray<crucible::TraceEntry, 1> guard_body_ops{};
        guard_body_ops.at(::fixy::index<0>()).schema_hash = SchemaHash{0x6AA6};

        // Each child builds its own arena: the parent's arena must not carry
        // a bump from a child that was about to abort.
        auto make = [&](crucible::LoopTermKind kind, uint32_t count, float epsilon) {
            return [&, kind, count, epsilon] {
                ::fixy::BumpArena child_arena(1 << 12);
                crucible::RegionNode& body = *crucible::make_region(test.alloc, child_arena, guard_body_ops.view());
                body.next = crucible::make_link(*crucible::make_terminal(test.alloc, child_arena));
                crucible::LoopNode& node =
                    *crucible::make_loop(test.alloc, child_arena, body, crucible::compute_body_content_hash(body),
                                         ::fixy::View<crucible::FeedbackEdge>{}, kind, count, epsilon);
                // Keeps the optimizer from deciding the call had no effect
                // and eliding the clause along with it.
                asm volatile("" ::"r"(&node) : "memory");
            };
        };

        // The control.  A well-formed UNTIL loop, and REPEAT with a zero
        // count, which runs the body no times and continues, run in the test
        // process.  If one aborts, the guard is over-tight.
        make(crucible::LoopTermKind::UNTIL, 3, 0.125f)();
        make(crucible::LoopTermKind::REPEAT, 0, 0.0f)();

        // NaN compares false against everything, so it passed the guard this
        // replaced -- `!(epsilon < 0.0f)` -- and reached the bit_cast in the
        // termination hash, where it is 2^24-2 distinct values.  An infinite
        // convergence threshold is not a threshold.
        constexpr ::fixy::TextView kNotFinite{"foundation: contract violation: ::fixy::fp::is_finite(epsilon)"};
        ::fixy::check::expect_dies(
            runner, make(crucible::LoopTermKind::UNTIL, 3, std::numeric_limits<float>::quiet_NaN()), kNotFinite);
        ::fixy::check::expect_dies(
            runner, make(crucible::LoopTermKind::UNTIL, 3, -std::numeric_limits<float>::quiet_NaN()), kNotFinite);
        ::fixy::check::expect_dies(
            runner, make(crucible::LoopTermKind::UNTIL, 3, std::numeric_limits<float>::infinity()), kNotFinite);
        ::fixy::check::expect_dies(runner, make(crucible::LoopTermKind::UNTIL, 3, -1.0f),
                                   ::fixy::TextView{"foundation: contract violation: epsilon >= 0.0f"});

        // An UNTIL count of zero says the loop never ran. replay would walk
        // it zero times and report success.
        ::fixy::check::expect_dies(
            runner, make(crucible::LoopTermKind::UNTIL, 0, 0.125f),
            ::fixy::TextView{"foundation: contract violation: term_kind != LoopTermKind::UNTIL || repeat_count > 0u"});

        ::fixy::report(::fixy::Sink::Out,
                       "  make_loop rejects NaN, infinite and negative epsilon, and an UNTIL count of zero\n");

        // A loaded branch keeps an arm with no target until a caller
        // resolves it.  The merkle hash of such a branch ends the process
        // with its reason, and it reads no null node.
        auto hash_unresolved_branch = [&] {
            ::fixy::BumpArena child_arena(1 << 12);
            crucible::BranchNode& branch = *child_arena.make<crucible::BranchNode>(test.alloc);
            branch.kind = crucible::TraceNodeKind::BRANCH;
            branch.set_arms(child_arena.array<crucible::BranchNode::Arm>(
                test.alloc, 1, crucible::BranchNode::Arm{1, crucible::TraceLink{}}));
            crucible::recompute_merkle(branch);
            // The child prints the hash only when the hash returns.
            ::fixy::report(::fixy::Sink::Out, "  the hash of the unresolved branch: {}\n", branch.merkle_hash.raw());
        };
        ::fixy::check::expect_dies(runner, hash_unresolved_branch,
                                   ::fixy::TextView{"an arm of the branch has no target"});

        ::fixy::report(::fixy::Sink::Out, "  the hash of a branch with an unresolved arm ends the process\n");

        // The door of a walk that knows the kind gives the node of its kind,
        // and it ends the process on a node of a different kind.  The child
        // prints a field of the node only when the door returns.
        enum class Door : uint8_t {
            REGION,
            BRANCH,
            LOOP
        };
        auto open_door = [&](crucible::TraceNodeKind node_kind, Door door) {
            return [&, node_kind, door] {
                ::fixy::BumpArena child_arena(1 << 12);
                crucible::RegionNode& body = *crucible::make_region(test.alloc, child_arena, guard_body_ops.view());
                body.next = crucible::make_link(*crucible::make_terminal(test.alloc, child_arena));
                crucible::TraceNode& loop_node =
                    *crucible::make_loop(test.alloc, child_arena, body, crucible::compute_body_content_hash(body),
                                         ::fixy::View<crucible::FeedbackEdge>{}, crucible::LoopTermKind::REPEAT, 1);
                crucible::TraceNode& node = node_kind == crucible::TraceNodeKind::LOOP ? loop_node : body;
                uint32_t field = 0;
                switch (door) {
                    case Door::REGION:
                        field = node.expect_region()->num_ops();
                        break;
                    case Door::BRANCH:
                        field = node.expect_branch()->num_arms();
                        break;
                    case Door::LOOP:
                        field = node.expect_loop()->repeat_count;
                        break;
                    default:
                        ::fixy::unreachable();
                }
                ::fixy::report(::fixy::Sink::Out, "  the door gave a node with the field {}\n", field);
            };
        };
        // The door of a region opens a region, and the door of a loop opens a
        // loop, in the test process.
        open_door(crucible::TraceNodeKind::REGION, Door::REGION)();
        open_door(crucible::TraceNodeKind::LOOP, Door::LOOP)();
        ::fixy::check::expect_dies(
            runner, open_door(crucible::TraceNodeKind::REGION, Door::BRANCH),
            ::fixy::TextView{"foundation: fatal invariant failed: kind == TraceNodeKind::BRANCH"});
        ::fixy::check::expect_dies(runner, open_door(crucible::TraceNodeKind::REGION, Door::LOOP),
                                   ::fixy::TextView{"foundation: fatal invariant failed: kind == TraceNodeKind::LOOP"});
        ::fixy::check::expect_dies(
            runner, open_door(crucible::TraceNodeKind::LOOP, Door::REGION),
            ::fixy::TextView{"foundation: fatal invariant failed: kind == TraceNodeKind::REGION"});

        ::fixy::report(::fixy::Sink::Out, "  the door of a known kind refuses a node of a different kind\n");

        // The door of the inputs and of the outputs takes each array of the
        // count together, so an array of a different count ends the process.
        // No door sets a count with no array, so no array is absent at a
        // count above zero.
        ::foundation::core::FixedArray<crucible::TensorMeta, 2> two_metas{};
        ::foundation::core::FixedArray<crucible::OpIndex, 2> two_producers{};
        ::foundation::core::FixedArray<crucible::SlotId, 2> two_slots{};
        ::foundation::core::FixedArray<crucible::SlotId, 1> one_slot{};
        // The refusal is a call of fixy::fatal, which the optimizer keeps.
        auto set_inputs = [&](::fixy::View<crucible::SlotId> slot_ids) {
            return [&, slot_ids] {
                crucible::TraceEntry entry{};
                entry.set_inputs(two_metas.view(), two_producers.view(), slot_ids);
            };
        };
        auto set_outputs = [&](::fixy::View<crucible::SlotId> slot_ids) {
            return [&, slot_ids] {
                crucible::TraceEntry entry{};
                entry.set_outputs(two_metas.view(), slot_ids);
            };
        };
        // Arrays of one count pass in the test process.
        set_inputs(two_slots.view())();
        set_outputs(two_slots.view())();
        constexpr ::fixy::TextView kOtherCount{
            "foundation: fatal: RunSet::set got a run of 1 elements for a slot of the count 2"};
        ::fixy::check::expect_dies(runner, set_inputs(one_slot.view()), kOtherCount);
        ::fixy::check::expect_dies(runner, set_outputs(one_slot.view()), kOtherCount);

        ::fixy::report(::fixy::Sink::Out, "  an entry refuses arrays of two counts for one kind\n");
    }

    crucible::test::pass("test_merkle_dag: all tests passed\n");
    return 0;
}
