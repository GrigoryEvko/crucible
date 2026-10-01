// The compiled replay of test_mlp_trace: the activation, three compiled
// iterations, and the check of the data flow between two ops.

#include "mlp_trace.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;

namespace test_mlp_trace {

void activate_compiled_mode(Vigil& vigil) {
    // Alignment costs the first few ops of an iteration: each one
    // extends a match against the head of the region and still returns
    // as recorded.  Only the rest of that iteration runs compiled.
    std::printf("\n── Activating COMPILED mode (K=%u alignment) ──\n", Vigil::ALIGNMENT_K);

    static constexpr uint32_t AK = Vigil::ALIGNMENT_K;

    for (uint32_t i = 0; i < AK; i++) {
        auto pkt = build_op(3, i);
        auto r = crucible::test::dispatch_synthetic(vigil, pkt.entry, pkt.metas, pkt.n_metas);
        assert(r.action == DispatchResult::Action::RECORD);
    }
    assert(vigil.context().is_compiled());
    std::printf("   Aligned after %u ops. CrucibleContext: COMPILED\n", AK);
    std::printf("   Pool: %llu bytes allocated\n",
                static_cast<unsigned long long>(vigil.context().pool().pool_bytes()));

    for (uint32_t i = AK; i < NUM_OPS; i++) {
        auto pkt = build_op(3, i);
        auto r = crucible::test::dispatch_synthetic(vigil, pkt.entry, pkt.metas, pkt.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
    std::printf("   Partial iteration completed (ops %u-%u COMPILED)\n", AK, NUM_OPS - 1);
}

void run_compiled_iterations(Vigil& vigil) {
    for (uint32_t iter = 4; iter <= 6; iter++) {
        std::printf("\n── Iteration %u: COMPILED ──\n", iter);

        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto pkt = build_op(iter, i);
            auto result = crucible::test::dispatch_synthetic(vigil, pkt.entry, pkt.metas, pkt.n_metas);

            assert(result.action == DispatchResult::Action::COMPILED);

            const char* status_str = (result.status == ReplayStatus::MATCH)    ? "MATCH"
                                   : (result.status == ReplayStatus::COMPLETE) ? "COMPLETE"
                                                                               : "DIVERGED";

            // The buffer already exists.  A real kernel would write its
            // result here rather than producing a new tensor.
            void* out = vigil.output_ptr(vigil.mint_producer_context(), 0);
            assert(out != nullptr);

            uint64_t tensor_bytes = 0;
            const auto& op = MLP_OPS[i];
            // The size is recomputed from the op index because this
            // stand-in has no kernel to ask.  A real replay reads it
            // from the recorded metadata.
            switch (i) {
                case 0:
                case 1:
                case 2:
                case 7:
                    tensor_bytes = BATCH * HIDDEN * 4;
                    break;
                case 3:
                case 4:
                case 5:
                case 6:
                    tensor_bytes = BATCH * OUT_DIM * 4;
                    break;
                case 8:
                case 9:
                    tensor_bytes = IN_DIM * HIDDEN * 4;
                    break;
                default:
                    break;
            }
            std::memset(out, static_cast<int>(0x10 + i), tensor_bytes);

            if (i < 3 || i == NUM_OPS - 1) {
                std::printf("   op%2u %-25s → %s  out=%p\n", i, op.name, status_str, out);
            } else if (i == 3) {
                std::printf("   ...  (ops 3-%u match)\n", NUM_OPS - 2);
            }
        }

        std::printf("   compiled_iterations=%u  diverged=%u\n", vigil.compiled_iterations(), vigil.diverged_count());
    }
}

void verify_data_flow(Vigil& vigil) {
    // The first op's output and the second op's input are the same pool
    // slot, so writing a pattern through one and reading it through the
    // other is what shows the recorded edges, the pool offsets and the
    // replay all agree.
    std::printf("\n── Data flow verification (iteration 7) ──\n");

    auto p0 = build_op(7, 0);
    auto r0 = crucible::test::dispatch_synthetic(vigil, p0.entry, p0.metas, p0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    std::memset(vigil.output_ptr(vigil.mint_producer_context(), 0), 0xAB, BATCH * HIDDEN * 4);

    auto p1 = build_op(7, 1);
    auto r1 = crucible::test::dispatch_synthetic(vigil, p1.entry, p1.metas, p1.n_metas);
    assert(r1.action == DispatchResult::Action::COMPILED);
    auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(vigil.mint_producer_context(), 0));
    bool flow_ok = true;
    for (uint32_t b = 0; b < BATCH * HIDDEN * 4; b++) {
        if (in_data[b] != 0xAB) {
            flow_ok = false;
            break;
        }
    }

    // Run out the rest of the iteration so the engine is left at a
    // boundary rather than part way through one.
    for (uint32_t i = 2; i < NUM_OPS; i++) {
        auto pkt = build_op(7, i);
        (void)crucible::test::dispatch_synthetic(vigil, pkt.entry, pkt.metas, pkt.n_metas);
    }

    std::printf("   op0 output → op1 input data flow: %s\n", flow_ok ? "VERIFIED" : "FAILED");
    assert(flow_ok);
}

}  // namespace test_mlp_trace
