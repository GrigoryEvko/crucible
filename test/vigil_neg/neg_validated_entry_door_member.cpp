// Negative-compile test.  This file must fail to compile.
//
// Violation: a call of the private retag of the door of the trust ladder.
//
// The member that writes the key is private, and only the two doors of the
// ladder (validate_entry and validate_entry_within_ceiling) call it, each
// after its check.

#include <crucible/ValidatedOp.h>

int main() {
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.num_inputs = 200;
                                                     return built;
                                                 }};
    const auto forged = crucible::detail::EntryLadderDoor::retag_(crucible::mint_ffi_entry(entry));
    return crucible::mint_validated_op(forged).is_some() ? 0 : 1;
}
