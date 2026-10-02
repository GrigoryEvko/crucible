#include <crucible/CKernel.h>
#include <crucible/MerkleDag.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include "padding_bytes.h"
#include "test_assert.h"
#include <cstdio>
#include <cstring>
#include <type_traits>

// Real schema hashes come from the adapter's hash function.  These are
// arbitrary, because what is under test is the table, not the hashing.
static const crucible::SchemaHash HASH_LINEAR{0xAAAA000000000001ULL};
static const crucible::SchemaHash HASH_CONV2D{0xBBBB000000000002ULL};
static const crucible::SchemaHash HASH_SDPA{0xCCCC000000000003ULL};
static const crucible::SchemaHash HASH_RELU{0xDDDD000000000004ULL};
static const crucible::SchemaHash HASH_EWISE_ADD{0xEEEE000000000005ULL};
static const crucible::SchemaHash HASH_UNKNOWN{0xDEAD000000000000ULL};

// Registration demands a hash carrying external provenance, so every
// fixture below tags its value to stand in for one that arrived from
// outside.
using ExtHash = ::fixy::Tagged<crucible::SchemaHash, ::fixy::tags::source::External>;

[[nodiscard]] static ExtHash external_hash(crucible::SchemaHash schema_hash) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External>(schema_hash);
}

// A mutable view asks for the context of a Vigil's producer claim.  These
// tests use no Vigil, so they take that context from the test door.
static constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

int main() {
    using namespace crucible;

    assert(classify_kernel(HASH_LINEAR) == CKernelId::OPAQUE);
    assert(classify_kernel(HASH_CONV2D) == CKernelId::OPAQUE);
    assert(classify_kernel(HASH_UNKNOWN) == CKernelId::OPAQUE);

    // Each registration below reaches an unsealed table, so each one lands.
    const auto register_or_fail = [](ExtHash schema_hash, CKernelId id) {
        const bool was_registered = register_schema_hash(kVigilForeground, schema_hash, id);
        assert(was_registered);
    };
    register_or_fail(external_hash(HASH_LINEAR), CKernelId::GEMM_LINEAR);
    register_or_fail(external_hash(HASH_CONV2D), CKernelId::CONV2D);
    register_or_fail(external_hash(HASH_SDPA), CKernelId::SDPA);
    register_or_fail(external_hash(HASH_RELU), CKernelId::ACT_RELU);
    register_or_fail(external_hash(HASH_EWISE_ADD), CKernelId::EWISE_ADD);

    assert(classify_kernel(HASH_LINEAR) == CKernelId::GEMM_LINEAR);
    assert(classify_kernel(HASH_CONV2D) == CKernelId::CONV2D);
    assert(classify_kernel(HASH_SDPA) == CKernelId::SDPA);
    assert(classify_kernel(HASH_RELU) == CKernelId::ACT_RELU);
    assert(classify_kernel(HASH_EWISE_ADD) == CKernelId::EWISE_ADD);

    assert(classify_kernel(HASH_UNKNOWN) == CKernelId::OPAQUE);
    assert(classify_kernel(SchemaHash{0ULL}) == CKernelId::OPAQUE);
    assert(classify_kernel(SchemaHash{UINT64_MAX}) == CKernelId::OPAQUE);

    // Lookup is a binary search, so the two values that fall outside the
    // registered range on either side are the interesting misses.
    assert(classify_kernel(SchemaHash{0x0001ULL}) == CKernelId::OPAQUE);
    assert(classify_kernel(SchemaHash{0xFFFF000000000010ULL}) == CKernelId::OPAQUE);

    // Registering the same pair twice must leave the answer unchanged.
    register_or_fail(external_hash(HASH_LINEAR), CKernelId::GEMM_LINEAR);
    assert(classify_kernel(HASH_LINEAR) == CKernelId::GEMM_LINEAR);

    assert(std::strcmp(ckernel_name(CKernelId::OPAQUE), "OPAQUE") == 0);
    assert(std::strcmp(ckernel_name(CKernelId::GEMM_LINEAR), "GEMM_LINEAR") == 0);
    assert(std::strcmp(ckernel_name(CKernelId::CONV2D), "CONV2D") == 0);
    assert(std::strcmp(ckernel_name(CKernelId::SDPA), "SDPA") == 0);
    assert(std::strcmp(ckernel_name(CKernelId::ACT_RELU), "ACT_RELU") == 0);
    assert(std::strcmp(ckernel_name(CKernelId::EWISE_ADD), "EWISE_ADD") == 0);

    // Trace building fills this field in.  Until then it defaults to
    // OPAQUE rather than to an arbitrary kernel.
    {
        TraceEntry te{};
        assert(te.kernel_id == CKernelId::OPAQUE);
        assert(te.is_mutable == false);
        te.kernel_id = CKernelId::GEMM_ADDMM;
        assert(te.kernel_id == CKernelId::GEMM_ADDMM);
    }

    // The fallback name for an unnamed id opens with an angle bracket,
    // which is what the second half of the assertion below catches.
    for (uint8_t i = 1; i < static_cast<uint8_t>(CKernelId::NUM_KERNELS); i++) {
        const char* name = ckernel_name(static_cast<CKernelId>(i));
        assert(name != nullptr && name[0] != '<' && "all known ids must have proper names");
    }

    // Rejection of an out-of-range byte is pinned by negative-compile
    // fixtures.  What this block pins is the accepting half: every byte
    // below the kernel count builds a validated raw without firing the
    // contract, and widening it yields the matching enumerator.  The
    // values chosen are the two ends of the range plus a mid-range
    // sample, so the coverage is not just the boundaries.
    {
        static_assert(sizeof(ValidCKernelIdRaw) == sizeof(uint8_t),
                      "ValidCKernelIdRaw must collapse to a bare uint8_t");

        // Zero is the lowest valid value, and the default of the field.
        {
            const ValidCKernelIdRaw raw = ::fixy::mint_refined<kValidCKernelIdBound>(uint8_t{0});
            assert(make_ckernel_id(raw) == CKernelId::OPAQUE);
        }

        // One below the kernel count is the highest valid value.
        {
            constexpr uint8_t LAST = static_cast<uint8_t>(CKernelId::NUM_KERNELS) - uint8_t{1};
            const ValidCKernelIdRaw raw = ::fixy::mint_refined<kValidCKernelIdBound>(LAST);
            assert(make_ckernel_id(raw) == CKernelId::COMM_BARRIER);
            assert(static_cast<uint8_t>(make_ckernel_id(raw)) == LAST);
        }

        // Widening must preserve the underlying byte, not renumber it.
        for (uint8_t v : {uint8_t{1}, uint8_t{42}, uint8_t{100}, static_cast<uint8_t>(CKernelId::SDPA),
                          static_cast<uint8_t>(CKernelId::CONV2D), static_cast<uint8_t>(CKernelId::ACT_RELU)}) {
            const ValidCKernelIdRaw raw = ::fixy::mint_refined<kValidCKernelIdBound>(v);
            const CKernelId id = make_ckernel_id(raw);
            assert(static_cast<uint8_t>(id) == v && "make_ckernel_id must preserve underlying byte");
        }
    }

    {
        CKernelTable t;
        assert(!t.is_sealed());
        const auto mv = t.mint_mutable_view(kVigilForeground);
        assert(mv.has_value());

        const bool was_registered = t.register_op(*mv, SchemaHash{0x1}, CKernelId::GEMM_MM);
        assert(was_registered);
        assert(t.count() == 1);

        t.seal();
        assert(t.is_sealed());
        // Sealing twice leaves the table sealed rather than unsealing it.
        t.seal();
        assert(t.is_sealed());

        // A sealed table mints no mutable view, in every build mode.
        assert(!t.mint_mutable_view(kVigilForeground).has_value());

        // A view minted before the seal proves only that the table was open
        // then.  Its write after the seal is refused and changes nothing.
        const bool was_added_after_seal = t.register_op(*mv, SchemaHash{0x3}, CKernelId::SDPA);
        assert(!was_added_after_seal);
        const bool was_replaced_after_seal = t.register_op(*mv, SchemaHash{0x1}, CKernelId::SDPA);
        assert(!was_replaced_after_seal);
        assert(t.count() == 1);
        assert(t.classify(SchemaHash{0x1}) == CKernelId::GEMM_MM);
        assert(t.classify(SchemaHash{0x3}) == CKernelId::OPAQUE);

        // Clearing resets the seal along with the entries.
        t.clear(::foundation::effects::testing::test());
        assert(!t.is_sealed());
        assert(t.count() == 0);

        const auto mv_after_clear = t.mint_mutable_view(kVigilForeground);
        assert(mv_after_clear.has_value());
        const bool was_registered_after_clear = t.register_op(*mv_after_clear, SchemaHash{0x2}, CKernelId::SDPA);
        assert(was_registered_after_clear);
        assert(t.classify(SchemaHash{0x2}) == CKernelId::SDPA);
    }

    {
        CKernelTable t;
        const auto mv = t.mint_mutable_view(kVigilForeground);
        assert(mv.has_value());
        const bool was_registered = t.register_op(*mv, SchemaHash{0x42}, CKernelId::CONV2D);
        assert(was_registered);
        assert(t.classify(SchemaHash{0x42}) == CKernelId::CONV2D);
    }

    // Classification is the read path a background thread takes, and it
    // must keep working once the table is sealed.
    {
        CKernelTable t;
        const auto mv = t.mint_mutable_view(kVigilForeground);
        assert(mv.has_value());
        const bool was_mm_registered = t.register_op(*mv, SchemaHash{0x111}, CKernelId::GEMM_MM);
        assert(was_mm_registered);
        const bool was_norm_registered = t.register_op(*mv, SchemaHash{0x222}, CKernelId::LAYER_NORM);
        assert(was_norm_registered);
        t.seal();
        assert(t.classify(SchemaHash{0x111}) == CKernelId::GEMM_MM);
        assert(t.classify(SchemaHash{0x222}) == CKernelId::LAYER_NORM);
        assert(t.classify(SchemaHash{0xBAD}) == CKernelId::OPAQUE);
        // Minting a sealed view is permitted once the table is sealed.
        (void)t.mint_sealed_view();
    }

    // The global table outlives this function, so it is returned to a
    // known state for whatever runs next in the same process.
    static_assert(std::is_same_v<decltype(global_ckernel_table()), CKernelTableSingleton>);

    // A registration after the seal is refused and changes nothing.
    global_ckernel_table().value()->seal();
    const bool was_registered_after_seal =
        register_schema_hash(kVigilForeground, external_hash(HASH_UNKNOWN), CKernelId::SDPA);
    assert(!was_registered_after_seal);
    assert(classify_kernel(HASH_UNKNOWN) == CKernelId::OPAQUE);

    global_ckernel_table().value()->clear(::foundation::effects::testing::test());

    // A table holds 256 entries, and each padding byte of an entry costs one
    // store at each initialization of an automatic table (padding_bytes.h).
    crucible::test::expect_no_padding_byte<^^CKernelEntry>();

    std::printf("test_ckernel: all tests passed\n");
    return 0;
}
