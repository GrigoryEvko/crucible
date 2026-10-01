// The pool behind the compiled iterations of test_vigil_dispatch: the data
// flow between the slots, and the bounds of each slot.

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;

namespace test_vigil_dispatch {

using crucible::test::flush_and_wait_region_published;

// Each op writes a distinct byte to its output and the next op reads it
// back from its input.  The pattern differs per op so that a stale or
// aliased slot shows up as the wrong byte rather than as a pass.
void test_dispatch_data_flow() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, 3);

    auto d0 = make_op(4, 0);
    auto r0 = crucible::test::dispatch_synthetic(vigil, d0.entry, d0.metas, d0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    assert(r0.status == ReplayStatus::MATCH);
    std::memset(vigil.output_ptr(vigil.mint_producer_context(), 0), 0x42, 4096);

    for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
        auto d = make_op(4, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
        assert(r.status == ReplayStatus::MATCH);

        auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(vigil.mint_producer_context(), 0));
        uint8_t expected = static_cast<uint8_t>(0x42 + i - 1);
        for (uint32_t b = 0; b < 4096; b++)
            assert(in_data[b] == expected);

        std::memset(vigil.output_ptr(vigil.mint_producer_context(), 0), static_cast<int>(0x42 + i), 4096);
    }

    auto d7 = make_op(4, NUM_OPS - 1);
    auto r7 = crucible::test::dispatch_synthetic(vigil, d7.entry, d7.metas, d7.n_metas);
    assert(r7.action == DispatchResult::Action::COMPILED);
    assert(r7.status == ReplayStatus::COMPLETE);

    auto* in_last = static_cast<uint8_t*>(vigil.input_ptr(vigil.mint_producer_context(), 0));
    uint8_t expected_last = static_cast<uint8_t>(0x42 + NUM_OPS - 2);
    for (uint32_t b = 0; b < 4096; b++)
        assert(in_last[b] == expected_last);

    // One iteration from the alignment helper, one from the loop above.
    assert(vigil.compiled_iterations() == 2);

    std::printf("  test_dispatch_data_flow: PASSED\n");
}

void test_dispatch_pool_bounds() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(vigil);
    align_and_activate(vigil, 3);

    auto& pool = vigil.context().pool();
    assert(pool.is_initialized());
    assert(pool.pool_bytes() > 0);

    auto* pool_base = static_cast<uint8_t*>(pool.pool_base());
    uint64_t pool_bytes = pool.pool_bytes();

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(4, i);
        auto r = crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);

        auto* p = static_cast<uint8_t*>(vigil.output_ptr(vigil.mint_producer_context(), 0));
        assert(p >= pool_base);
        assert(p + 4096 <= pool_base + pool_bytes);
    }

    assert(vigil.compiled_iterations() == 2);
    assert(pool.num_external() == 0);

    std::printf("  test_dispatch_pool_bounds: PASSED\n");
}

}  // namespace test_vigil_dispatch
