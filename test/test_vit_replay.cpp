// The compiled replay of test_vit: the alignment, a thousand compiled
// iterations, and the check of the data flow from the attention to the
// output projection.

#include "vit.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "tensor_data.h"
#include <chrono>
#include <cstdint>
#include <cstring>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_vit {

void align_and_complete_iteration(Vigil& vigil) {
    // The context turns compiled only once K ops line up with the
    // recorded region, so the first AK dispatches still record.
    static constexpr uint32_t AK = Vigil::ALIGNMENT_K;
    for (uint32_t i = 0; i < AK; i++) {
        auto ap = build_op(i, 3);
        auto ar = crucible::test::dispatch_synthetic(vigil, ap.entry, ap.metas, ap.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, ar.action == DispatchResult::Action::RECORD);
    }
    CRUCIBLE_TEST_ASSERT(kTest, vigil.context().is_compiled());
    // Complete partial iteration.
    for (uint32_t i = AK; i < NUM_OPS; i++) {
        auto ap = build_op(i, 3);
        auto ar = crucible::test::dispatch_synthetic(vigil, ap.entry, ap.metas, ap.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, ar.action == DispatchResult::Action::COMPILED);
    }
    ::fixy::report(::fixy::Sink::Out, "\n   CrucibleContext: COMPILED (aligned after {} ops)\n\n", AK);
}

double run_compiled_iterations(Vigil& vigil) {
    ::fixy::report(::fixy::Sink::Out, "── 1000 compiled iterations ──\n");

    auto t0 = std::chrono::steady_clock::now();

    for (uint32_t iter = 4; iter < 1004; iter++) {
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto p = build_op(i, iter);
            // The timed loop calls dispatch_op inline, so the time is that of
            // the inlined hot path and of the copy of the records into a
            // sealed list, and not that of a helper call.
            auto r = crucible::test::with_synthetic_op(
                p.entry, p.metas, p.n_metas,
                [&vigil](crucible::ValidatedOp const& op) { return vigil.dispatch_op(op); });
            CRUCIBLE_TEST_ASSERT(kTest, r.action == DispatchResult::Action::COMPILED);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    uint64_t total_ops = 1000ULL * NUM_OPS;
    double ns_op = double(us) * 1000.0 / double(total_ops);

    ::fixy::report(::fixy::Sink::Out, "   {} dispatches in {} μs ({} ns/op)\n",
                   static_cast<unsigned long long>(total_ops), static_cast<long long>(us), ns_op);
    ::fixy::report(::fixy::Sink::Out, "   compiled_iterations={}  diverged={}\n", vigil.compiled_iterations(),
                   vigil.diverged_count());
    return ns_op;
}

void verify_attention_data_flow(Vigil& vigil) {
    ::fixy::report(::fixy::Sink::Out, "\n── Data flow: sdpa(op7) → out_proj(op8) ──\n");

    for (uint32_t i = 0; i < 7; i++) {
        auto p = build_op(i, 9999);
        auto r = crucible::test::dispatch_synthetic(vigil, p.entry, p.metas, p.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, r.action == DispatchResult::Action::COMPILED);
    }
    // A recognizable byte pattern goes into the attention output.  If the
    // next op reads the same buffer back, the two slots are one.
    auto p7 = build_op(7, 9999);
    auto r7 = crucible::test::dispatch_synthetic(vigil, p7.entry, p7.metas, p7.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, r7.action == DispatchResult::Action::COMPILED);
    std::memset(crucible::test::host_ptr(vigil.output_ptr(vigil.mint_producer_context(), crucible::OutputPosition{0})),
                0xCD, 64);

    auto p8 = build_op(8, 9999);
    auto r8 = crucible::test::dispatch_synthetic(vigil, p8.entry, p8.metas, p8.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, r8.action == DispatchResult::Action::COMPILED);

    auto* in_data =
        crucible::test::host_ptr<uint8_t>(vigil.input_ptr(vigil.mint_producer_context(), crucible::InputPosition{0}));
    bool ok = true;
    for (uint32_t i = 0; i < 64; i++)
        if (in_data[i] != 0xCD) {
            ok = false;
            break;
        }

    // Complete the iteration
    for (uint32_t i = 9; i < NUM_OPS; i++) {
        auto p = build_op(i, 9999);
        (void)crucible::test::dispatch_synthetic(vigil, p.entry, p.metas, p.n_metas);
    }

    ::fixy::report(::fixy::Sink::Out, "   {}\n", ok ? ::fixy::TextView{"VERIFIED"} : ::fixy::TextView{"FAILED"});
    CRUCIBLE_TEST_ASSERT(kTest, ok);
}

}  // namespace test_vit
