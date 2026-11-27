// Negative-compile test.  This file must fail to compile.
//
// Violation: mint_validated_op with a named ViewMut of the records.
//
// The ViewMut of a frame is the one writer of its run.  A name that stays
// with the caller after the mint could write the records of the op.  The
// mint takes the ViewMut by move, so the name gives its run up.

#include <crucible/ValidatedOp.h>

int main() {
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.schema_hash = crucible::SchemaHash{0x1234};
                                                     built.num_inputs = 1;
                                                     return built;
                                                 }};
    crucible::OpRecordStack stack{};
    return stack.with_frame(
        ::foundation::core::WindowCount{1},
        [&entry](auto slots) noexcept -> int {
            const auto op = crucible::mint_validated_op(
                crucible::validate_entry(crucible::mint_ffi_entry(entry)).expect("the trust ladder admits the entry"),
                slots);
            return op.is_some() ? 0 : 1;
        },
        [] noexcept -> int { return 2; });
}
