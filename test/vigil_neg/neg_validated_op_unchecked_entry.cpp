// Negative-compile test.  This file must fail to compile.
//
// Violation: mint_validated_op with an entry that the trust ladder did not
// check.
//
// An adapter fills an entry from the values of a foreign runtime, and the Ref
// of its sealed entry carries the tag FromPytorch.  Only the checks of the
// ladder retag it to Validated, and mint_validated_op takes only the second
// tag.  So an entry that skipped the checks reaches no recording entry point.

#include <crucible/ValidatedOp.h>

int main() {
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed,
                                                 []() noexcept { return crucible::TraceRing::Entry{}; }};
    const auto op = crucible::mint_validated_op(crucible::mint_ffi_entry(entry));
    return op.is_some() ? 0 : 1;
}
