#include <crucible/CKernel.h>
#include <crucible/MerkleDag.h>
#include <crucible/safety/Tagged.h>
#include <cassert>
#include <cstdio>
#include <cstring>

// Fake schema hashes — real values come from the Vessel's hash function.
// We use arbitrary values to test the table mechanics.
static const crucible::SchemaHash HASH_LINEAR   {0xAAAA000000000001ULL};
static const crucible::SchemaHash HASH_CONV2D   {0xBBBB000000000002ULL};
static const crucible::SchemaHash HASH_SDPA     {0xCCCC000000000003ULL};
static const crucible::SchemaHash HASH_RELU     {0xDDDD000000000004ULL};
static const crucible::SchemaHash HASH_EWISE_ADD{0xEEEE000000000005ULL};
static const crucible::SchemaHash HASH_UNKNOWN  {0xDEAD000000000000ULL};

// Simulated-Vessel tag wrapper.  register_schema_hash() requires the
// hash to carry source::External provenance — tests explicitly retag
// their fixtures to document "this path pretends to be Vessel".
using ExtHash = crucible::safety::Tagged<crucible::SchemaHash,
                                          crucible::safety::source::External>;

int main() {
    using namespace crucible;

    // ── Table starts empty: everything is OPAQUE ────────────────────────
    assert(classify_kernel(HASH_LINEAR)    == CKernelId::OPAQUE);
    assert(classify_kernel(HASH_CONV2D)    == CKernelId::OPAQUE);
    assert(classify_kernel(HASH_UNKNOWN)   == CKernelId::OPAQUE);

    // ── Register known ops (simulating Vessel startup registration) ──────
    register_schema_hash(ExtHash{HASH_LINEAR},    CKernelId::GEMM_LINEAR);
    register_schema_hash(ExtHash{HASH_CONV2D},    CKernelId::CONV2D);
    register_schema_hash(ExtHash{HASH_SDPA},      CKernelId::SDPA);
    register_schema_hash(ExtHash{HASH_RELU},      CKernelId::ACT_RELU);
    register_schema_hash(ExtHash{HASH_EWISE_ADD}, CKernelId::EWISE_ADD);

    // ── Registered ops resolve correctly ────────────────────────────────
    assert(classify_kernel(HASH_LINEAR)    == CKernelId::GEMM_LINEAR);
    assert(classify_kernel(HASH_CONV2D)    == CKernelId::CONV2D);
    assert(classify_kernel(HASH_SDPA)      == CKernelId::SDPA);
    assert(classify_kernel(HASH_RELU)      == CKernelId::ACT_RELU);
    assert(classify_kernel(HASH_EWISE_ADD) == CKernelId::EWISE_ADD);

    // ── Unknown hash still returns OPAQUE ───────────────────────────────
    assert(classify_kernel(HASH_UNKNOWN)   == CKernelId::OPAQUE);
    assert(classify_kernel(SchemaHash{0ULL})           == CKernelId::OPAQUE);
    assert(classify_kernel(SchemaHash{UINT64_MAX})     == CKernelId::OPAQUE);

    // ── Binary search boundary: hash smaller than all registered ────────
    assert(classify_kernel(SchemaHash{0x0001ULL})      == CKernelId::OPAQUE);

    // ── Binary search boundary: hash larger than all registered ─────────
    assert(classify_kernel(SchemaHash{0xFFFF000000000010ULL}) == CKernelId::OPAQUE);

    // ── Idempotent re-registration (same hash, same id) ─────────────────
    // Second registration of same hash: table should still return correct id.
    register_schema_hash(ExtHash{HASH_LINEAR}, CKernelId::GEMM_LINEAR);
    assert(classify_kernel(HASH_LINEAR) == CKernelId::GEMM_LINEAR);

    // ── ckernel_name() sanity ────────────────────────────────────────────
    assert(std::strcmp(ckernel_name(CKernelId::OPAQUE),      "OPAQUE")      == 0);
    assert(std::strcmp(ckernel_name(CKernelId::GEMM_LINEAR), "GEMM_LINEAR") == 0);
    assert(std::strcmp(ckernel_name(CKernelId::CONV2D),      "CONV2D")      == 0);
    assert(std::strcmp(ckernel_name(CKernelId::SDPA),        "SDPA")        == 0);
    assert(std::strcmp(ckernel_name(CKernelId::ACT_RELU),    "ACT_RELU")    == 0);
    assert(std::strcmp(ckernel_name(CKernelId::EWISE_ADD),   "EWISE_ADD")   == 0);

    // ── TraceEntry.kernel_id is populated by build_trace() ───────────────
    // Verify the field exists at the right offset and defaults to OPAQUE.
    {
        TraceEntry te{};
        assert(te.kernel_id == CKernelId::OPAQUE);
        assert(te.is_mutable == false);
        te.kernel_id = CKernelId::GEMM_ADDMM;
        assert(te.kernel_id == CKernelId::GEMM_ADDMM);
    }

    // ── Full taxonomy: verify all non-OPAQUE ids have non-empty names ────
    for (uint8_t i = 1; i < static_cast<uint8_t>(CKernelId::NUM_KERNELS); i++) {
        const char* name = ckernel_name(static_cast<CKernelId>(i));
        assert(name != nullptr && name[0] != '<' && "all known ids must have proper names");
    }

    // ── Seal lifecycle: default Mutable, seal() flips, clear() resets ────
    {
        CKernelTable t;
        assert(!t.is_sealed());

        // Populate before seal().
        t.register_op(SchemaHash{0x1}, CKernelId::GEMM_MM);
        assert(t.count() == 1);

        t.seal();
        assert(t.is_sealed());
        // Idempotent: re-seal keeps state sealed.
        t.seal();
        assert(t.is_sealed());

        // clear() resets the seal alongside the entries.
        t.clear();
        assert(!t.is_sealed());
        assert(t.count() == 0);

        // Post-clear: register works again.
        t.register_op(SchemaHash{0x2}, CKernelId::SDPA);
        assert(t.classify(SchemaHash{0x2}) == CKernelId::SDPA);
    }

    // ── Typed register_op(MutableView, ...) compiles and works ──────────
    {
        CKernelTable t;
        auto mv = t.mint_mutable_view();
        t.register_op(mv, SchemaHash{0x42}, CKernelId::CONV2D);
        assert(t.classify(SchemaHash{0x42}) == CKernelId::CONV2D);
    }

    // ── Classify works post-seal (bg-thread read path) ──────────────────
    {
        CKernelTable t;
        t.register_op(SchemaHash{0x111}, CKernelId::GEMM_MM);
        t.register_op(SchemaHash{0x222}, CKernelId::LAYER_NORM);
        t.seal();
        assert(t.classify(SchemaHash{0x111}) == CKernelId::GEMM_MM);
        assert(t.classify(SchemaHash{0x222}) == CKernelId::LAYER_NORM);
        assert(t.classify(SchemaHash{0xBAD})  == CKernelId::OPAQUE);
        // mint_sealed_view is allowed post-seal.
        (void)t.mint_sealed_view();
    }

    // Restore global table to a clean Mutable state so downstream tests
    // running in the same process start from a known baseline.
    global_ckernel_table().clear();

    std::printf("test_ckernel: all tests passed\n");
    return 0;
}
