#include <crucible/CKernel.h>
#include <crucible/MerkleDag.h>
#include <cassert>
#include <cstdio>
#include <cstring>

// Fake schema hashes — real values come from the Vessel's hash function.
// We use arbitrary uint64_t values to test the table mechanics.
static constexpr uint64_t HASH_LINEAR    = 0xAAAA000000000001ULL;
static constexpr uint64_t HASH_CONV2D    = 0xBBBB000000000002ULL;
static constexpr uint64_t HASH_SDPA      = 0xCCCC000000000003ULL;
static constexpr uint64_t HASH_RELU      = 0xDDDD000000000004ULL;
static constexpr uint64_t HASH_EWISE_ADD = 0xEEEE000000000005ULL;
static constexpr uint64_t HASH_UNKNOWN   = 0xDEAD000000000000ULL;

int main() {
    using namespace crucible;

    // ── Table starts empty: everything is OPAQUE ────────────────────────
    assert(classify_kernel(HASH_LINEAR)    == CKernelId::OPAQUE);
    assert(classify_kernel(HASH_CONV2D)    == CKernelId::OPAQUE);
    assert(classify_kernel(HASH_UNKNOWN)   == CKernelId::OPAQUE);

    // ── Register known ops (simulating Vessel startup registration) ──────
    register_schema_hash(HASH_LINEAR,    CKernelId::GEMM_LINEAR);
    register_schema_hash(HASH_CONV2D,    CKernelId::CONV2D);
    register_schema_hash(HASH_SDPA,      CKernelId::SDPA);
    register_schema_hash(HASH_RELU,      CKernelId::ACT_RELU);
    register_schema_hash(HASH_EWISE_ADD, CKernelId::EWISE_ADD);

    // ── Registered ops resolve correctly ────────────────────────────────
    assert(classify_kernel(HASH_LINEAR)    == CKernelId::GEMM_LINEAR);
    assert(classify_kernel(HASH_CONV2D)    == CKernelId::CONV2D);
    assert(classify_kernel(HASH_SDPA)      == CKernelId::SDPA);
    assert(classify_kernel(HASH_RELU)      == CKernelId::ACT_RELU);
    assert(classify_kernel(HASH_EWISE_ADD) == CKernelId::EWISE_ADD);

    // ── Unknown hash still returns OPAQUE ───────────────────────────────
    assert(classify_kernel(HASH_UNKNOWN)   == CKernelId::OPAQUE);
    assert(classify_kernel(0ULL)           == CKernelId::OPAQUE);
    assert(classify_kernel(UINT64_MAX)     == CKernelId::OPAQUE);

    // ── Binary search boundary: hash smaller than all registered ────────
    assert(classify_kernel(0x0001ULL)      == CKernelId::OPAQUE);

    // ── Binary search boundary: hash larger than all registered ─────────
    assert(classify_kernel(0xFFFF000000000010ULL) == CKernelId::OPAQUE);

    // ── Idempotent re-registration (same hash, same id) ─────────────────
    // Second registration of same hash: table should still return correct id.
    register_schema_hash(HASH_LINEAR, CKernelId::GEMM_LINEAR);
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
        assert(te.pad_te == 0);
        te.kernel_id = CKernelId::GEMM_ADDMM;
        assert(te.kernel_id == CKernelId::GEMM_ADDMM);
    }

    // ── Full taxonomy: verify all non-OPAQUE ids have non-empty names ────
    for (uint8_t i = 1; i < static_cast<uint8_t>(CKernelId::NUM_KERNELS); i++) {
        const char* name = ckernel_name(static_cast<CKernelId>(i));
        assert(name != nullptr && name[0] != '<' && "all known ids must have proper names");
    }

    std::printf("test_ckernel: all tests passed\n");
    return 0;
}
