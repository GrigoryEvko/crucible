// The scalar slots of a trace entry and the overflow digest of the vessel.
//
// The content hash of a region folds the scalar slots and the input metadata
// of each entry.  An operation with more scalars than the inline slots, or
// more input tensors than the metadata limit, must still give two different
// entries when only a value past a limit differs.  An operation that fits the
// limits must give the entry that a path with no digest gives.

#include "../vessel/torch/entry_slots.h"

#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>

#include <cstdint>

#include "test_assert.h"

namespace {

using crucible::TraceRing;
using crucible::vessel::InputOverflow;
using crucible::vessel::kInlineScalars;
using crucible::vessel::ScalarArgs;
using crucible::vessel::write_scalar_slots;

// The entry of an operation whose scalars are 1, 2, ... count, with the last
// scalar replaced by last.  O(count).
[[nodiscard]] TraceRing::Entry entry_of(uint32_t count, int64_t last, InputOverflow const& inputs) {
    ScalarArgs<true> scalars{};
    for (uint32_t index = 1; index < count; index++)
        scalars.push(static_cast<int64_t>(index));
    if (count != 0) scalars.push(last);
    TraceRing::Entry entry{};
    write_scalar_slots(entry, scalars, inputs);
    return entry;
}

[[nodiscard]] bool same_slots(TraceRing::Entry const& left, TraceRing::Entry const& right) {
    if (left.num_scalar_args != right.num_scalar_args) return false;
    for (uint16_t slot = 0; slot < kInlineScalars; slot++) {
        if (left.scalar_values[slot] != right.scalar_values[slot]) return false;
    }
    return true;
}

// The input tensor of rank one with the given extent.
[[nodiscard]] crucible::TensorMeta vector_meta(int64_t extent) {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(extent);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    return meta;
}

void test_fitting_operation_keeps_its_scalars() {
    ScalarArgs<false> bounded{};
    for (int64_t value = 1; value <= kInlineScalars + 1; value++)
        bounded.push(value);
    TraceRing::Entry plain{};
    write_scalar_slots(plain, bounded);
    assert(plain.num_scalar_args == kInlineScalars);

    TraceRing::Entry const digested = entry_of(kInlineScalars, kInlineScalars, InputOverflow{});
    assert(same_slots(plain, digested));
    assert(digested.num_scalar_args == kInlineScalars);
    assert(digested.scalar_values[kInlineScalars - 1] == kInlineScalars);
}

void test_scalar_past_the_slots_reaches_the_digest() {
    constexpr uint32_t kStepSizes = 136;
    TraceRing::Entry const first = entry_of(kStepSizes, 7, InputOverflow{});
    TraceRing::Entry const second = entry_of(kStepSizes, 8, InputOverflow{});
    TraceRing::Entry const again = entry_of(kStepSizes, 7, InputOverflow{});
    assert(first.num_scalar_args == kInlineScalars);
    assert(!same_slots(first, second));
    assert(same_slots(first, again));
    for (uint16_t slot = 0; slot + 1 < kInlineScalars; slot++)
        assert(first.scalar_values[slot] == static_cast<int64_t>(slot) + 1);
}

void test_scalar_count_reaches_the_digest() {
    TraceRing::Entry const shorter = entry_of(6, 6, InputOverflow{});
    TraceRing::Entry const longer = entry_of(7, 6, InputOverflow{});
    assert(!same_slots(shorter, longer));
}

void test_input_past_the_limit_reaches_the_digest() {
    InputOverflow narrow{};
    narrow.fold(vector_meta(3));
    InputOverflow wide{};
    wide.fold(vector_meta(5));

    TraceRing::Entry const first = entry_of(2, 2, narrow);
    TraceRing::Entry const second = entry_of(2, 2, wide);
    assert(first.num_scalar_args == 3);
    assert(first.scalar_values[0] == 1 && first.scalar_values[1] == 2);
    assert(!same_slots(first, second));

    TraceRing::Entry const no_scalars = entry_of(0, 0, narrow);
    assert(no_scalars.num_scalar_args == 1);
}

}  // namespace

int main() {
    test_fitting_operation_keeps_its_scalars();
    test_scalar_past_the_slots_reaches_the_digest();
    test_scalar_count_reaches_the_digest();
    test_input_past_the_limit_reaches_the_digest();
    crucible::test::pass("test_vessel_entry_slots: PASS\n");
    return 0;
}
