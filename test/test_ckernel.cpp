#include <crucible/CKernel.h>
#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "padding_bytes.h"
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};

    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_LINEAR) == CKernelId::OPAQUE);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_CONV2D) == CKernelId::OPAQUE);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_UNKNOWN) == CKernelId::OPAQUE);

    // Each registration below reaches an unsealed table, so each one lands.
    const auto register_or_fail = [&test](ExtHash schema_hash, CKernelId id) {
        const bool was_registered = register_schema_hash(kVigilForeground, schema_hash, id);
        CRUCIBLE_TEST_ASSERT(test, was_registered);
    };
    register_or_fail(external_hash(HASH_LINEAR), CKernelId::GEMM_LINEAR);
    register_or_fail(external_hash(HASH_CONV2D), CKernelId::CONV2D);
    register_or_fail(external_hash(HASH_SDPA), CKernelId::SDPA);
    register_or_fail(external_hash(HASH_RELU), CKernelId::ACT_RELU);
    register_or_fail(external_hash(HASH_EWISE_ADD), CKernelId::EWISE_ADD);

    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_LINEAR) == CKernelId::GEMM_LINEAR);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_CONV2D) == CKernelId::CONV2D);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_SDPA) == CKernelId::SDPA);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_RELU) == CKernelId::ACT_RELU);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_EWISE_ADD) == CKernelId::EWISE_ADD);

    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_UNKNOWN) == CKernelId::OPAQUE);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(SchemaHash{0ULL}) == CKernelId::OPAQUE);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(SchemaHash{UINT64_MAX}) == CKernelId::OPAQUE);

    // Lookup is a binary search, so the two values that fall outside the
    // registered range on either side are the interesting misses.
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(SchemaHash{0x0001ULL}) == CKernelId::OPAQUE);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(SchemaHash{0xFFFF000000000010ULL}) == CKernelId::OPAQUE);

    // Registering the same pair twice must leave the answer unchanged.
    register_or_fail(external_hash(HASH_LINEAR), CKernelId::GEMM_LINEAR);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_LINEAR) == CKernelId::GEMM_LINEAR);

    CRUCIBLE_TEST_ASSERT(test, ckernel_name(CKernelId::OPAQUE) == ::fixy::TextView{"OPAQUE"});
    CRUCIBLE_TEST_ASSERT(test, ckernel_name(CKernelId::GEMM_LINEAR) == ::fixy::TextView{"GEMM_LINEAR"});
    CRUCIBLE_TEST_ASSERT(test, ckernel_name(CKernelId::CONV2D) == ::fixy::TextView{"CONV2D"});
    CRUCIBLE_TEST_ASSERT(test, ckernel_name(CKernelId::SDPA) == ::fixy::TextView{"SDPA"});
    CRUCIBLE_TEST_ASSERT(test, ckernel_name(CKernelId::ACT_RELU) == ::fixy::TextView{"ACT_RELU"});
    CRUCIBLE_TEST_ASSERT(test, ckernel_name(CKernelId::EWISE_ADD) == ::fixy::TextView{"EWISE_ADD"});
    static_assert(ckernel_name(CKernelId::COMM_BARRIER) == ::fixy::TextView{"COMM_BARRIER"});

    // Trace building fills this field in.  Until then it defaults to
    // OPAQUE rather than to an arbitrary kernel.
    {
        TraceEntry te{};
        CRUCIBLE_TEST_ASSERT(test, te.kernel_id == CKernelId::OPAQUE);
        CRUCIBLE_TEST_ASSERT(test, te.is_mutable == false);
        te.kernel_id = CKernelId::GEMM_ADDMM;
        CRUCIBLE_TEST_ASSERT(test, te.kernel_id == CKernelId::GEMM_ADDMM);
    }

    // The fallback name for an unnamed id opens with an angle bracket,
    // which is what the second half of the assertion below catches.
    for (uint8_t i = 1; std::size_t{i} < ckernel_id_count; i++) {
        const ::fixy::TextView name = ckernel_name(static_cast<CKernelId>(i));
        const bool opens_with_an_angle_bracket =
            name.window(0, 1).match([](::fixy::TextView first) noexcept { return first == ::fixy::TextView{"<"}; },
                                    [] noexcept { return false; });
        CRUCIBLE_TEST_ASSERT(test, name.size() != 0 && !opens_with_an_angle_bracket
                                       && "all known ids must have proper names");
    }

    // A byte widens into a CKernelId through enum_from.  Each byte below the
    // count of the ids gives the id of that byte, the first byte past the
    // last id gives none, and so does each larger byte.  The loader refuses
    // such a byte in an image (test_serialize_release_gate).
    for (unsigned raw = 0; raw <= 255; ++raw) {
        const uint8_t byte = static_cast<uint8_t>(raw);
        const ::fixy::Option<CKernelId> id = ::fixy::enum_from<CKernelId>(byte);
        const bool names_an_id = std::size_t{byte} < ckernel_id_count;
        CRUCIBLE_TEST_ASSERT(test, id.is_some() == names_an_id);
        CRUCIBLE_TEST_ASSERT(test, id.is_none_or([byte](CKernelId named) noexcept {
            return static_cast<uint8_t>(named) == byte;
        }) && "enum_from preserves the byte of the id");
    }
    CRUCIBLE_TEST_ASSERT(test, ::fixy::enum_from<CKernelId>(uint8_t{0}).is_some_and([](CKernelId named) noexcept {
        return named == CKernelId::OPAQUE;
    }));
    CRUCIBLE_TEST_ASSERT(test,
                         ::fixy::enum_from<CKernelId>(static_cast<uint8_t>(ckernel_id_count - 1))
                             .is_some_and([](CKernelId named) noexcept { return named == CKernelId::COMM_BARRIER; }));

    {
        CKernelTable t;
        CRUCIBLE_TEST_ASSERT(test, !t.is_sealed());
        const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");

        const bool was_registered = t.register_op(kTest, mv, SchemaHash{0x1}, CKernelId::GEMM_MM);
        CRUCIBLE_TEST_ASSERT(test, was_registered);
        CRUCIBLE_TEST_ASSERT(test, t.count() == 1);

        t.seal(kTest);
        CRUCIBLE_TEST_ASSERT(test, t.is_sealed());
        // Sealing twice leaves the table sealed rather than unsealing it.
        t.seal(kTest);
        CRUCIBLE_TEST_ASSERT(test, t.is_sealed());

        // A sealed table mints no mutable view, in every build mode.
        CRUCIBLE_TEST_ASSERT(test, t.mint_mutable_view(kVigilForeground).is_none());

        // A view minted before the seal proves only that the table was open
        // then.  Its write after the seal is refused and changes nothing.
        const bool was_added_after_seal = t.register_op(kTest, mv, SchemaHash{0x3}, CKernelId::SDPA);
        CRUCIBLE_TEST_ASSERT(test, !was_added_after_seal);
        const bool was_replaced_after_seal = t.register_op(kTest, mv, SchemaHash{0x1}, CKernelId::SDPA);
        CRUCIBLE_TEST_ASSERT(test, !was_replaced_after_seal);
        CRUCIBLE_TEST_ASSERT(test, t.count() == 1);
        CRUCIBLE_TEST_ASSERT(test, t.classify(SchemaHash{0x1}) == CKernelId::GEMM_MM);
        CRUCIBLE_TEST_ASSERT(test, t.classify(SchemaHash{0x3}) == CKernelId::OPAQUE);

        // Clearing resets the seal along with the entries.
        t.clear(::foundation::effects::testing::test());
        CRUCIBLE_TEST_ASSERT(test, !t.is_sealed());
        CRUCIBLE_TEST_ASSERT(test, t.count() == 0);

        const auto mv_after_clear = t.mint_mutable_view(kVigilForeground).expect("a cleared table is open");
        const bool was_registered_after_clear = t.register_op(kTest, mv_after_clear, SchemaHash{0x2}, CKernelId::SDPA);
        CRUCIBLE_TEST_ASSERT(test, was_registered_after_clear);
        CRUCIBLE_TEST_ASSERT(test, t.classify(SchemaHash{0x2}) == CKernelId::SDPA);
    }

    {
        CKernelTable t;
        const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
        const bool was_registered = t.register_op(kTest, mv, SchemaHash{0x42}, CKernelId::CONV2D);
        CRUCIBLE_TEST_ASSERT(test, was_registered);
        CRUCIBLE_TEST_ASSERT(test, t.classify(SchemaHash{0x42}) == CKernelId::CONV2D);
    }

    // Classification is the read path a background thread takes, and it
    // must keep working once the table is sealed.
    {
        CKernelTable t;
        const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
        const bool was_mm_registered = t.register_op(kTest, mv, SchemaHash{0x111}, CKernelId::GEMM_MM);
        CRUCIBLE_TEST_ASSERT(test, was_mm_registered);
        const bool was_norm_registered = t.register_op(kTest, mv, SchemaHash{0x222}, CKernelId::LAYER_NORM);
        CRUCIBLE_TEST_ASSERT(test, was_norm_registered);
        t.seal(kTest);
        CRUCIBLE_TEST_ASSERT(test, t.classify(SchemaHash{0x111}) == CKernelId::GEMM_MM);
        CRUCIBLE_TEST_ASSERT(test, t.classify(SchemaHash{0x222}) == CKernelId::LAYER_NORM);
        CRUCIBLE_TEST_ASSERT(test, t.classify(SchemaHash{0xBAD}) == CKernelId::OPAQUE);
        // Minting a sealed view is permitted once the table is sealed.
        (void)t.mint_sealed_view();
    }

    // The global table outlives this function, so it is returned to a
    // known state for whatever runs next in the same process.
    static_assert(std::is_same_v<decltype(global_ckernel_table()), CKernelTableSingleton>);

    // A registration after the seal is refused and changes nothing.
    global_ckernel_table().value()->seal(kTest);
    const bool was_registered_after_seal =
        register_schema_hash(kVigilForeground, external_hash(HASH_UNKNOWN), CKernelId::SDPA);
    CRUCIBLE_TEST_ASSERT(test, !was_registered_after_seal);
    CRUCIBLE_TEST_ASSERT(test, classify_kernel(HASH_UNKNOWN) == CKernelId::OPAQUE);

    global_ckernel_table().value()->clear(::foundation::effects::testing::test());

    // A table holds 256 entries, and each padding byte of an entry costs one
    // store at each initialization of an automatic table (padding_bytes.h).
    crucible::test::expect_no_padding_byte<^^CKernelEntry>(kTest);

    ::fixy::check::pass(test, "test_ckernel: all tests passed\n");
    return 0;
}
