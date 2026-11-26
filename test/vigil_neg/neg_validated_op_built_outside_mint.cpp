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
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.num_inputs = 1;
                                                     return built;
                                                 }};
    const crucible::ValidatedOp op{::fixy::erase_brand(::fixy::to(entry)),
                                   crucible::mint_checked_meta_run(::fixy::View<const crucible::TensorMeta>{})};
    return static_cast<int>(op.entry().num_inputs);
}
