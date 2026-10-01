// The compiled replay of test_vit: the alignment, a thousand compiled
// iterations, and the check of the data flow from the attention to the
// output projection.

#include "vit.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;

namespace test_vit {

void align_and_complete_iteration(Vigil& vigil) {
    // The context turns compiled only once K ops line up with the
    // recorded region, so the first AK dispatches still record.
    static constexpr uint32_t AK = Vigil::ALIGNMENT_K;
    for (uint32_t i = 0; i < AK; i++) {
        auto ap = build_op(i, 3);
        auto ar = vigil.dispatch_op(crucible::test::certify_synthetic_entry(ap.entry), ap.metas, ap.n_metas);
        assert(ar.action == DispatchResult::Action::RECORD);
    }
    assert(vigil.context().is_compiled());
    // Complete partial iteration.
    for (uint32_t i = AK; i < NUM_OPS; i++) {
        auto ap = build_op(i, 3);
        auto ar = vigil.dispatch_op(crucible::test::certify_synthetic_entry(ap.entry), ap.metas, ap.n_metas);
        assert(ar.action == DispatchResult::Action::COMPILED);
    }
    std::printf("\n   CrucibleContext: COMPILED (aligned after %u ops)\n\n", AK);
}

double run_compiled_iterations(Vigil& vigil) {
    std::printf("── 1000 compiled iterations ──\n");

    auto t0 = std::chrono::steady_clock::now();

    for (uint32_t iter = 4; iter < 1004; iter++) {
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto p = build_op(i, iter);
            auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(p.entry), p.metas, p.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    uint64_t total_ops = 1000ULL * NUM_OPS;
    double ns_op = double(us) * 1000.0 / double(total_ops);

    std::printf("   %llu dispatches in %lld μs (%.1f ns/op)\n", static_cast<unsigned long long>(total_ops),
                static_cast<long long>(us), ns_op);
    std::printf("   compiled_iterations=%u  diverged=%u\n", vigil.compiled_iterations(), vigil.diverged_count());
    return ns_op;
}

void verify_attention_data_flow(Vigil& vigil) {
    std::printf("\n── Data flow: sdpa(op7) → out_proj(op8) ──\n");

    for (uint32_t i = 0; i < 7; i++) {
        auto p = build_op(i, 9999);
        auto r = vigil.dispatch_op(crucible::test::certify_synthetic_entry(p.entry), p.metas, p.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
    // A recognizable byte pattern goes into the attention output.  If the
    // next op reads the same buffer back, the two slots are one.
    auto p7 = build_op(7, 9999);
    auto r7 = vigil.dispatch_op(crucible::test::certify_synthetic_entry(p7.entry), p7.metas, p7.n_metas);
    assert(r7.action == DispatchResult::Action::COMPILED);
    std::memset(vigil.output_ptr(vigil.mint_producer_context(), 0), 0xCD, 64);

    auto p8 = build_op(8, 9999);
    auto r8 = vigil.dispatch_op(crucible::test::certify_synthetic_entry(p8.entry), p8.metas, p8.n_metas);
    assert(r8.action == DispatchResult::Action::COMPILED);

    auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(vigil.mint_producer_context(), 0));
    bool ok = true;
    for (uint32_t i = 0; i < 64; i++)
        if (in_data[i] != 0xCD) {
            ok = false;
            break;
        }

    // Complete the iteration
    for (uint32_t i = 9; i < NUM_OPS; i++) {
        auto p = build_op(i, 9999);
        (void)vigil.dispatch_op(crucible::test::certify_synthetic_entry(p.entry), p.metas, p.n_metas);
    }

    std::printf("   %s\n", ok ? "VERIFIED" : "FAILED");
    assert(ok);
}

}  // namespace test_vit
