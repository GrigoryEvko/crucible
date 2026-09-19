// Integration tests for compiled replay across the whole pipeline, from
// the trace ring through the background thread and back out of
// dispatch_op.  Nothing here is mocked, so a failure can come from any
// stage and the assertions say which one.

#include <crucible/Vigil.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_FxAliases.h>
#include "test_harness.h"
#include "test_assert.h"
#include "test_abort_probe.h"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace crucible;

static constexpr uint32_t NUM_OPS = 8;
static constexpr uint32_t K = Vigil::ALIGNMENT_K;

static constexpr SchemaHash SCHEMA[NUM_OPS] = {SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102},
                                               SchemaHash{0x103}, SchemaHash{0x104}, SchemaHash{0x105},
                                               SchemaHash{0x106}, SchemaHash{0x107}};
static constexpr ShapeHash SHAPE[NUM_OPS] = {ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
                                             ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207}};

static void* fake_ptr(uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

static TensorMeta make_meta(void* data_ptr) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(1024);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = external_data_ptr(data_ptr);
    return m;
}

struct OpData {
    TraceRing::Entry entry{};
    TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

static OpData make_op(uint32_t iter, uint32_t op_idx) {
    OpData d;
    d.entry.schema_hash = SCHEMA[op_idx];
    d.entry.shape_hash = SHAPE[op_idx];
    d.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0) d.metas[idx++] = make_meta(fake_ptr(iter, op_idx - 1));
    d.metas[idx++] = make_meta(fake_ptr(iter, op_idx));
    d.n_metas = static_cast<uint16_t>(d.entry.num_inputs + d.entry.num_outputs);
    return d;
}

static void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
}

static void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
}

using test::flush_and_wait_compiled;

// Callers get back an engine sitting at position 0 and ready for whole
// compiled iterations.
//
// Reaching that state takes two phases.  The first K ops still return
// RECORD: each one extends a sliding-window match against the head of
// the region, and only the last of them confirms the match and
// activates.  The remaining ops of that iteration then run compiled,
// which is what leaves the engine back at position 0.
static void align_and_activate(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(iter, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD && "alignment ops should return RECORD");
    }
    assert(vigil.context().is_compiled() && "CrucibleContext should be compiled after K alignment ops");

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

static void test_dispatch_basic() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);

    align_and_activate(vigil, 3);
    assert(vigil.compiled_iterations() == 1);

    uint32_t match_count = 0;
    uint32_t complete_count = 0;
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(4, i);
        auto result = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);

        if (result.status == ReplayStatus::MATCH)
            match_count++;
        else if (result.status == ReplayStatus::COMPLETE)
            complete_count++;
    }

    assert(match_count == NUM_OPS - 1);
    assert(complete_count == 1);
    assert(vigil.compiled_iterations() == 2);

    std::printf("  test_dispatch_basic: PASSED\n");
}

static void test_dispatch_divergence() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, 3);

    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(4, i);
        auto result = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);
        assert(result.status == ReplayStatus::MATCH);
    }

    // The shape still matches at this position, so only the schema can
    // be what trips the guard.
    TraceRing::Entry bad_entry{};
    bad_entry.schema_hash = SchemaHash{0xBAD};
    bad_entry.shape_hash = SHAPE[3];
    bad_entry.num_inputs = 1;
    bad_entry.num_outputs = 1;
    TensorMeta bad_metas[2]{};
    bad_metas[0] = make_meta(fake_ptr(4, 2));
    bad_metas[1] = make_meta(fake_ptr(4, 3));

    auto result = vigil.dispatch_op(crucible::vouch(bad_entry), bad_metas, 2);
    assert(result.action == DispatchResult::Action::RECORD);
    assert(result.status == ReplayStatus::DIVERGED);
    assert(vigil.diverged_count() == 1);

    auto d = make_op(4, 4);
    auto result2 = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    assert(result2.action == DispatchResult::Action::RECORD);
    assert(!vigil.context().is_compiled());

    std::printf("  test_dispatch_divergence: PASSED\n");
}

static void test_dispatch_recovery() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);

    align_and_activate(vigil, 3);

    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xBAD};
    bad.shape_hash = SHAPE[0];
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    TensorMeta bad_meta = make_meta(fake_ptr(99, 0));

    auto r_div = vigil.dispatch_op(crucible::vouch(bad), &bad_meta, 1);
    assert(r_div.action == DispatchResult::Action::RECORD);
    assert(r_div.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    // The ring still holds the alignment and divergence noise, so a
    // couple of clean iterations would not be enough to give the
    // background thread two clean boundaries to work from.  Six is.
    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, iter);
    feed_trigger(vigil, 16);

    // Divergence put the mode back to recording, so this call is waiting
    // for a fresh transition rather than observing the original one.
    flush_and_wait_compiled(vigil);

    align_and_activate(vigil, 17);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(18, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_dispatch_recovery: PASSED\n");
}

// Each op writes a distinct byte to its output and the next op reads it
// back from its input.  The pattern differs per op so that a stale or
// aliased slot shows up as the wrong byte rather than as a pass.
static void test_dispatch_data_flow() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, 3);

    auto d0 = make_op(4, 0);
    auto r0 = vigil.dispatch_op(crucible::vouch(d0.entry), d0.metas, d0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    assert(r0.status == ReplayStatus::MATCH);
    std::memset(vigil.output_ptr(0), 0x42, 4096);

    for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
        auto d = make_op(4, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
        assert(r.status == ReplayStatus::MATCH);

        auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
        uint8_t expected = static_cast<uint8_t>(0x42 + i - 1);
        for (uint32_t b = 0; b < 4096; b++)
            assert(in_data[b] == expected);

        std::memset(vigil.output_ptr(0), static_cast<int>(0x42 + i), 4096);
    }

    auto d7 = make_op(4, NUM_OPS - 1);
    auto r7 = vigil.dispatch_op(crucible::vouch(d7.entry), d7.metas, d7.n_metas);
    assert(r7.action == DispatchResult::Action::COMPILED);
    assert(r7.status == ReplayStatus::COMPLETE);

    auto* in_last = static_cast<uint8_t*>(vigil.input_ptr(0));
    uint8_t expected_last = static_cast<uint8_t>(0x42 + NUM_OPS - 2);
    for (uint32_t b = 0; b < 4096; b++)
        assert(in_last[b] == expected_last);

    // One iteration from the alignment helper, one from the loop above.
    assert(vigil.compiled_iterations() == 2);

    std::printf("  test_dispatch_data_flow: PASSED\n");
}

static void test_dispatch_pool_bounds() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, 3);

    auto& pool = vigil.context().pool();
    assert(pool.is_initialized());
    assert(pool.pool_bytes() > 0);

    auto* pool_base = static_cast<uint8_t*>(pool.pool_base());
    uint64_t pool_bytes = pool.pool_bytes();

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(4, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);

        auto* p = static_cast<uint8_t*>(vigil.output_ptr(0));
        assert(p >= pool_base);
        assert(p + 4096 <= pool_base + pool_bytes);
    }

    assert(vigil.compiled_iterations() == 2);
    assert(pool.num_external() == 0);

    std::printf("  test_dispatch_pool_bounds: PASSED\n");
}

// dispatch_op_pure carries an effect row in its template parameter and
// must otherwise behave exactly like dispatch_op.  The row is erased
// before it reaches the state machine, so every caller row has to reach
// the same result on every leg.
namespace eff = ::crucible::effects;

static void test_dispatch_pure_FOUND_I19() {
    // Default row, explicit empty row, and each pure alias.
    {
        Vigil vigil;
        auto d0 = make_op(0, 0);
        auto r0 = vigil.dispatch_op_pure(crucible::vouch(d0.entry), d0.metas, d0.n_metas);
        assert(r0.action == DispatchResult::Action::RECORD);

        auto d1 = make_op(0, 1);
        auto r1 = vigil.dispatch_op_pure<eff::Row<>>(crucible::vouch(d1.entry), d1.metas, d1.n_metas);
        assert(r1.action == DispatchResult::Action::RECORD);

        auto d2 = make_op(0, 2);
        auto r2 = vigil.dispatch_op_pure<eff::PureRow>(crucible::vouch(d2.entry), d2.metas, d2.n_metas);
        assert(r2.action == DispatchResult::Action::RECORD);

        auto d3 = make_op(0, 3);
        auto r3 = vigil.dispatch_op_pure<eff::TotRow>(crucible::vouch(d3.entry), d3.metas, d3.n_metas);
        assert(r3.action == DispatchResult::Action::RECORD);

        auto d4 = make_op(0, 4);
        auto r4 = vigil.dispatch_op_pure<eff::GhostRow>(crucible::vouch(d4.entry), d4.metas, d4.n_metas);
        assert(r4.action == DispatchResult::Action::RECORD);
    }

    // Alternating the two entry points must not perturb ring order.
    {
        Vigil vigil;
        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(0, i);
            DispatchResult r;
            if (i % 2 == 0) {
                r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
            } else {
                r = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
            }
            assert(r.action == DispatchResult::Action::RECORD);
        }
    }

    {
        Vigil vigil;
        feed_record(vigil, 0);
        feed_record(vigil, 1);
        feed_trigger(vigil, 2);
        flush_and_wait_compiled(vigil);

        for (uint32_t i = 0; i < K; ++i) {
            auto d = make_op(3, i);
            auto r = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::RECORD && "alignment via _pure should still RECORD");
        }
        assert(vigil.context().is_compiled() && "CrucibleContext should be compiled after K _pure aligns");

        for (uint32_t i = K; i < NUM_OPS; ++i) {
            auto d = make_op(3, i);
            auto r = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(4, i);
            auto r = vigil.dispatch_op_pure<eff::PureRow>(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    static_assert(eff::IsPure<eff::Row<>>);
    static_assert(eff::IsPure<eff::PureRow>);
    static_assert(eff::IsPure<eff::TotRow>);
    static_assert(eff::IsPure<eff::GhostRow>);
    static_assert(!eff::IsPure<eff::DivRow>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::IO>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Bg>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Alloc>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Init>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Test>>);
    static_assert(!eff::IsPure<eff::AllRow>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::IO, eff::Effect::Block>>);

    std::printf("  test_dispatch_pure_FOUND_I19: PASSED\n");
}

// The legs the test above does not reach: divergence, recovery after
// divergence, and the aliases other than PureRow on a compiled
// iteration.  Covering only PureRow on the compiled leg would let a
// regression that special-cased one alias pass, because the other
// aliases are exercised on the record leg alone.
static void test_dispatch_pure_FOUND_I19_AUDIT() {
    // Divergence.
    {
        Vigil vigil;
        feed_record(vigil, 0);
        feed_record(vigil, 1);
        feed_trigger(vigil, 2);
        flush_and_wait_compiled(vigil);
        align_and_activate(vigil, 3);

        for (uint32_t i = 0; i < 3; ++i) {
            auto d = make_op(4, i);
            auto r = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
            assert(r.status == ReplayStatus::MATCH);
        }

        // The shape still matches, so only the schema trips the guard.
        TraceRing::Entry bad_entry{};
        bad_entry.schema_hash = SchemaHash{0xBAD};
        bad_entry.shape_hash = SHAPE[3];
        bad_entry.num_inputs = 1;
        bad_entry.num_outputs = 1;
        TensorMeta bad_metas[2]{};
        bad_metas[0] = make_meta(fake_ptr(4, 2));
        bad_metas[1] = make_meta(fake_ptr(4, 3));

        auto rdiv = vigil.dispatch_op_pure(crucible::vouch(bad_entry), bad_metas, 2);
        assert(rdiv.action == DispatchResult::Action::RECORD);
        assert(rdiv.status == ReplayStatus::DIVERGED);
        assert(vigil.diverged_count() == 1);
        assert(!vigil.context().is_compiled());

        auto d = make_op(4, 4);
        auto r2 = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r2.action == DispatchResult::Action::RECORD);
    }

    // Divergence and recovery end to end.
    {
        Vigil vigil;
        feed_record(vigil, 0);
        feed_record(vigil, 1);
        feed_trigger(vigil, 2);
        flush_and_wait_compiled(vigil);
        align_and_activate(vigil, 3);

        TraceRing::Entry bad{};
        bad.schema_hash = SchemaHash{0xBAD};
        bad.shape_hash = SHAPE[0];
        bad.num_inputs = 0;
        bad.num_outputs = 1;
        TensorMeta bad_meta = make_meta(fake_ptr(99, 0));

        auto rdiv = vigil.dispatch_op_pure(crucible::vouch(bad), &bad_meta, 1);
        assert(rdiv.action == DispatchResult::Action::RECORD);
        assert(rdiv.status == ReplayStatus::DIVERGED);
        assert(!vigil.context().is_compiled());

        // Six clean iterations, for the same reason the non-wrapper
        // recovery test needs six: the ring still holds the alignment
        // and divergence noise.
        for (uint32_t iter = 10; iter < 16; iter++)
            feed_record(vigil, iter);
        feed_trigger(vigil, 16);
        flush_and_wait_compiled(vigil);

        // Realignment is open-coded rather than delegated to the helper,
        // because the helper drives dispatch_op and this leg has to be
        // driven through the wrapper as well.
        for (uint32_t i = 0; i < K; ++i) {
            auto d = make_op(17, i);
            auto r = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::RECORD);
        }
        assert(vigil.context().is_compiled());

        for (uint32_t i = K; i < NUM_OPS; ++i) {
            auto d = make_op(17, i);
            auto r = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(18, i);
            auto r = vigil.dispatch_op_pure(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    // The remaining aliases on a compiled iteration.
    {
        Vigil vigil;
        feed_record(vigil, 0);
        feed_record(vigil, 1);
        feed_trigger(vigil, 2);
        flush_and_wait_compiled(vigil);
        align_and_activate(vigil, 3);

        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(4, i);
            auto r = vigil.dispatch_op_pure<eff::TotRow>(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
            assert(r.status == ReplayStatus::MATCH || r.status == ReplayStatus::COMPLETE);
        }

        for (uint32_t i = 0; i < NUM_OPS; ++i) {
            auto d = make_op(5, i);
            auto r = vigil.dispatch_op_pure<eff::GhostRow>(crucible::vouch(d.entry), d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
            assert(r.status == ReplayStatus::MATCH || r.status == ReplayStatus::COMPLETE);
        }
    }

    std::printf("  test_dispatch_pure_FOUND_I19_AUDIT: "
                "(audit-A diverged + audit-B recovery + "
                "audit-C alias×COMPILED) PASSED\n");
}

// The recording ring is single-producer. Two threads calling dispatch_op
// claim the same slot and the later write erases the earlier one, with no
// diagnostic and no crash — the trace simply comes out short and wrong. A
// backward pass under a foreign runtime dispatches part of its operations on
// a worker thread of its own, so this is reachable from a first real model
// rather than from a later rewrite.
//
// dispatch_op therefore claims the first thread that reaches it and rejects
// every other one, in every build mode. This proves the rejection happens,
// and the same source runs under the release preset where a contract clause
// would have been compiled out.
//
// The rejection ends the process by design, so test::aborts arms a jump on
// the intruder thread and catches it there. The arming is thread-local, which
// is what lets the guard fire on a thread other than the one running main.
static void test_second_producer_is_rejected() {
    bool rejected = false;
    {
        Vigil vigil;

        // Claims this thread as the producer.
        auto first = make_op(0, 0);
        auto r0 = vigil.dispatch_op(crucible::vouch(first.entry), first.metas, first.n_metas);
        assert(r0.action == DispatchResult::Action::RECORD);

        std::thread intruder([&vigil, &rejected] {
            rejected = crucible::test::aborts([&vigil] {
                auto second = make_op(0, 1);
                (void)vigil.dispatch_op(crucible::vouch(second.entry), second.metas, second.n_metas);
            });
        });
        intruder.join();
    }

    if (!rejected) {
        std::fprintf(stderr, "  test_second_producer_is_rejected: a second thread reached the ring\n");
        std::abort();
    }

    // The same call from the claiming thread keeps working, so the gate is a
    // gate and not a blanket refusal.
    Vigil vigil;
    auto d = make_op(0, 0);
    auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    assert(r.action == DispatchResult::Action::RECORD);
    auto again = make_op(0, 1);
    auto r2 = vigil.dispatch_op(crucible::vouch(again.entry), again.metas, again.n_metas);
    assert(r2.action == DispatchResult::Action::RECORD);

    std::printf("  test_second_producer_is_rejected: PASSED\n");
}

int main() {
    std::printf("test_vigil_dispatch:\n");
    test_second_producer_is_rejected();
    test_dispatch_basic();
    test_dispatch_divergence();
    test_dispatch_recovery();
    test_dispatch_data_flow();
    test_dispatch_pool_bounds();
    test_dispatch_pure_FOUND_I19();
    test_dispatch_pure_FOUND_I19_AUDIT();
    std::printf("test_vigil_dispatch: all tests passed\n");
    return 0;
}
