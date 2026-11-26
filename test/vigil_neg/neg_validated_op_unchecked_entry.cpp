// Negative-compile test.  This file must fail to compile.
//
// Violation: mint_validated_op with an entry that the trust ladder did not
// check.
//
// An adapter fills an entry from the values of a foreign runtime, and its
// copy in the tag carries the tag FromPytorch.  Only the checks of the ladder
// retag it to Validated, and mint_validated_op takes only the second tag.  So
// an entry that skipped the checks reaches no recording entry point.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::TraceRing::Entry entry{};
    const auto op = crucible::mint_validated_op(crucible::mint_ffi_entry(entry));
    return op.is_some() ? 0 : 1;
}
