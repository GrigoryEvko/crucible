// The data scalars of a replayed region (ScalarRole of crucible/Types.h),
// driven through dispatch_op.  A loop whose step size changes at each
// iteration gets a region and replays it with no divergence, the replay binds
// the value of each data slot of the live op, and a changed structural scalar
// diverges.  The guard words come from compute_shape_hash of the vessel, so
// the Vigil sees the words that the record path gives.

#include "vigil_dispatch.h"

#include <crucible/Vigil.h>
#include "../vessel/torch/entry_slots.h"
#include "test_assert.h"
#include "test_harness.h"

#include <cstdint>

using namespace crucible;

namespace test_vigil_dispatch {
namespace {

using crucible::vessel::ScalarArgs;

struct IterationTag {};
using Iteration = ::fixy::Id<IterationTag, uint32_t>;

// The op whose first scalar is the step size of the iteration, a data
// scalar, and whose second scalar is a dimension, a structural scalar.
constexpr uint32_t kStepOp = 2;
// The op whose one scalar is twice the step size, a data scalar.  Its index
// is K, so the alignment replays it.
constexpr uint32_t kScaleOp = K;
static_assert(kStepOp < K && kScaleOp < NUM_OPS);

// The scalars that the loop gives the two ops of an iteration.
struct LoopScalars {
    double step_size = 0.0;
    int64_t dimension = 1;
};

// The scalars of the iteration: the step size changes at each iteration, and
// the dimension stays.
[[nodiscard]] LoopScalars scalars_of(Iteration iter) {
    return LoopScalars{.step_size = 1e-3 * static_cast<double>(iter.raw() + 1), .dimension = 1};
}

// The bits of the step size of the iteration, as a slot holds them.
[[nodiscard]] int64_t step_bits(Iteration iter) { return ::fixy::bit_cast<int64_t>(scalars_of(iter).step_size); }

// The op of the loop at the index, with the scalars, and the guard word that
// the record path gives it.
[[nodiscard]] OpData make_scalar_op(Iteration iter, OpIndex op, LoopScalars scalars) {
    OpData d = make_op(iter.raw(), op.raw());
    ScalarArgs<false> slots{};
    if (op.raw() == kStepOp) {
        slots.push(::fixy::bit_cast<int64_t>(scalars.step_size), ScalarType2::FLOAT, ScalarRole::DATA);
        slots.push(scalars.dimension, ScalarType2::INT, ScalarRole::STRUCTURAL);
    } else if (op.raw() == kScaleOp) {
        slots.push(::fixy::bit_cast<int64_t>(2.0 * scalars.step_size), ScalarType2::FLOAT, ScalarRole::DATA);
    }
    crucible::vessel::write_scalar_slots(d.entry, slots);
    d.entry.shape_hash = crucible::vessel::compute_shape_hash(crucible::test::records_of(d.metas, d.entry.num_inputs),
                                                              d.entry, OpDepth{});
    return d;
}

// Records count ops of the iteration.
void record_ops(Vigil& vigil, Iteration iter, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const OpData d = make_scalar_op(iter, OpIndex{i}, scalars_of(iter));
        (void)vigil.record_op(crucible::test::certify_synthetic_op(d.entry, d.metas, d.n_metas));
    }
}

// Dispatches op i of the iteration with the scalars.
[[nodiscard]] DispatchResult dispatch_scalar_op(Vigil& vigil, Iteration iter, OpIndex op, LoopScalars scalars) {
    const OpData d = make_scalar_op(iter, op, scalars);
    return crucible::test::dispatch_synthetic(vigil, d.entry, d.metas, d.n_metas);
}

// The value of the data slot of the op that the replay bound last.
[[nodiscard]] int64_t bound_value_of(Vigil& vigil, OpIndex op) {
    const VigilFgCtx fg = vigil.mint_producer_context();
    for (const Vigil::BoundScalar& bound : vigil.bound_scalars(fg)) {
        if (bound.op == op) return bound.value;
    }
    ::fixy::fatal("the region binds no data slot of the op");
}

}  // namespace

// The step size changes at each iteration, and the guard word of its op
// does not, so the detector finds the period and the region replays.  After
// each replayed op, its data slot holds the value of the live op of this
// iteration, and not the value of the recording.  On the old guard word, each
// step size gave its op another word: the loop had no period, and the wait
// for a region below failed at its bound.
void test_dispatch_binds_each_data_scalar() {
    Vigil vigil;
    record_ops(vigil, Iteration{0}, NUM_OPS);
    record_ops(vigil, Iteration{1}, NUM_OPS);
    record_ops(vigil, Iteration{2}, IterationDetector::K);
    crucible::test::flush_and_wait_region_published(vigil);

    // The first K ops align, and the region activates at the last of them.
    const Iteration aligned{3};
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        const DispatchResult result = dispatch_scalar_op(vigil, aligned, OpIndex{i}, scalars_of(aligned));
        CRUCIBLE_FATAL_INVARIANT(result.is_compiled() == (i >= K));
    }
    {
        const VigilFgCtx fg = vigil.mint_producer_context();
        CRUCIBLE_FATAL_INVARIANT(vigil.bound_scalars(fg).size() == 2 && "the region has two data slots");
    }
    // The alignment replays the op of the scale, so its slot holds the value
    // of this iteration.
    CRUCIBLE_FATAL_INVARIANT(bound_value_of(vigil, OpIndex{kScaleOp})
                             == ::fixy::bit_cast<int64_t>(2.0 * scalars_of(aligned).step_size));

    const uint32_t compiled_before = vigil.compiled_iterations();
    for (uint32_t iteration = 4; iteration < 8; iteration++) {
        const Iteration iter{iteration};
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            const DispatchResult result = dispatch_scalar_op(vigil, iter, OpIndex{i}, scalars_of(iter));
            CRUCIBLE_FATAL_INVARIANT(result.is_compiled() && "a new step size replays as a match");
            if (i == kStepOp) CRUCIBLE_FATAL_INVARIANT(bound_value_of(vigil, OpIndex{kStepOp}) == step_bits(iter));
        }
        CRUCIBLE_FATAL_INVARIANT(bound_value_of(vigil, OpIndex{kScaleOp})
                                 == ::fixy::bit_cast<int64_t>(2.0 * scalars_of(iter).step_size));
    }
    CRUCIBLE_FATAL_INVARIANT(vigil.diverged_count() == 0);
    CRUCIBLE_FATAL_INVARIANT(vigil.compiled_iterations() == compiled_before + 4);

    // A changed dimension is a structural scalar, so the op diverges, and the
    // ops before it still match.
    const Iteration changed{8};
    LoopScalars wide = scalars_of(changed);
    wide.dimension = 2;
    for (uint32_t i = 0; i < kStepOp; i++)
        CRUCIBLE_FATAL_INVARIANT(dispatch_scalar_op(vigil, changed, OpIndex{i}, wide).is_compiled());
    const DispatchResult diverged = dispatch_scalar_op(vigil, changed, OpIndex{kStepOp}, wide);
    CRUCIBLE_FATAL_INVARIANT(diverged.is_record() && diverged.status == ReplayStatus::DIVERGED);
    CRUCIBLE_FATAL_INVARIANT(vigil.diverged_count() == 1);

    crucible::test::pass("  test_dispatch_binds_each_data_scalar: PASSED\n");
}

}  // namespace test_vigil_dispatch
