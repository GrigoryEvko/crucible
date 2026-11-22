// Negative-compile test.  This file must fail to compile.
//
// Violation: a ValidatedOp built with no call of mint_validated_op.
//
// The constructor of ValidatedOp is private, and only the door class of
// mint_validated_op can call it.  A caller that builds the op directly would
// skip the check that the count of the records agrees with the entry, and
// the build would then read the records of a different op.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::TraceRing::Entry entry{};
    entry.num_inputs = 1;
    const crucible::ValidatedOp op{crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                   ::fixy::View<const crucible::TensorMeta>{}};
    return static_cast<int>(op.entry().num_inputs);
}
