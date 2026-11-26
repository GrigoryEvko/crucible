// The compiled replay of test_resnet: the alignment, a thousand compiled
// iterations, and the check of the data flow from the first op to the
// second.

#include "resnet.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "tensor_data.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace test_resnet {

void align_and_complete_iteration(Vigil& vigil, const std::vector<OpDef>& ops) {
    // Compiled execution starts only at an iteration boundary, so the
    // first few operations of this iteration are still recorded while
    // the pipeline aligns.
    static constexpr uint32_t AK = Vigil::ALIGNMENT_K;
    for (uint32_t i = 0; i < AK; i++) {
        auto ap = build_pkt(ops[i], 3);
        auto ar = crucible::test::dispatch_synthetic(vigil, ap.entry, ap.metas, ap.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, ar.action == DispatchResult::Action::RECORD);
    }
    CRUCIBLE_TEST_ASSERT(kTest, vigil.context().is_compiled());
    // The rest of the same iteration runs compiled.
    for (size_t i = AK; i < ops.size(); i++) {
        auto ap = build_pkt(ops[i], 3);
        auto ar = crucible::test::dispatch_synthetic(vigil, ap.entry, ap.metas, ap.n_metas);
        CRUCIBLE_TEST_ASSERT(kTest, ar.action == DispatchResult::Action::COMPILED);
    }
    std::printf("  aligned (%u ops) + partial iteration compiled\n", AK);
}

void run_compiled_iterations(Vigil& vigil, const std::vector<OpDef>& ops) {
    auto t0 = std::chrono::steady_clock::now();

    for (uint32_t iter = 4; iter < 1004; iter++) {
        for (size_t i = 0; i < ops.size(); i++) {
            auto p = build_pkt(ops[i], iter);
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
    uint64_t total = 1000ULL * ops.size();
    double ns_op = double(us) * 1000.0 / double(total);

    std::printf("  %llu dispatches in %lld us (%.1f ns/op)\n", static_cast<unsigned long long>(total),
                static_cast<long long>(us), ns_op);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.compiled_iterations() == 1001);  // 1 partial + 1000 full
    CRUCIBLE_TEST_ASSERT(kTest, vigil.diverged_count() == 0);
}

void verify_data_flow(Vigil& vigil, const std::vector<OpDef>& ops) {
    // The first operation's output slot must be the second
    // operation's input slot.  Writing a pattern through one and
    // reading it back through the other is what shows the slots were
    // planned to coincide.
    auto p0 = build_pkt(ops[0], 9999);
    auto r0 = crucible::test::dispatch_synthetic(vigil, p0.entry, p0.metas, p0.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, r0.action == DispatchResult::Action::COMPILED);
    std::memset(crucible::test::host_ptr(vigil.output_ptr(vigil.mint_producer_context(), crucible::OutputPosition{0})
                                             .expect("the replayed op has an output")),
                0xAB, 64);

    auto p1 = build_pkt(ops[1], 9999);
    auto r1 = crucible::test::dispatch_synthetic(vigil, p1.entry, p1.metas, p1.n_metas);
    CRUCIBLE_TEST_ASSERT(kTest, r1.action == DispatchResult::Action::COMPILED);

    auto* d =
        crucible::test::host_ptr<uint8_t>(vigil.input_ptr(vigil.mint_producer_context(), crucible::InputPosition{0})
                                              .expect("the replayed op has an input"));
    bool flow_ok = true;
    for (int i = 0; i < 64; i++)
        if (d[i] != 0xAB) {
            flow_ok = false;
            break;
        }

    // Finish the iteration so the pipeline is left at a boundary.
    for (size_t i = 2; i < ops.size(); i++) {
        auto p = build_pkt(ops[i], 9999);
        (void)crucible::test::dispatch_synthetic(vigil, p.entry, p.metas, p.n_metas);
    }

    std::printf("  data_flow: %s\n", flow_ok ? "VERIFIED" : "FAILED");
    CRUCIBLE_TEST_ASSERT(kTest, flow_ok);
}

}  // namespace test_resnet
