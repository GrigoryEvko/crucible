// Negative-compile test.  This file must fail to compile.
//
// Violation: mint_validated_op with a named ViewMut of the records.
//
// The ViewMut of a frame is the one writer of its run.  A name that stays
// with the caller after the mint could write the records of the op.  The
// mint takes the ViewMut by move, so the name gives its run up.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::OpRecordStack stack{};
    return stack.with_frame(
        ::foundation::core::WindowCount{1},
        [](auto slots) noexcept -> int {
            crucible::TraceRing::Entry entry{};
            entry.schema_hash = crucible::SchemaHash{0x1234};
            entry.num_inputs = 1;
            const auto op = crucible::mint_validated_op(
                crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(), slots);
            return op.is_some() ? 0 : 1;
        },
        [] noexcept -> int { return 2; });
}
