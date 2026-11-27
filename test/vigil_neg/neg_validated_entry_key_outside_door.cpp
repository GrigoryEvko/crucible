// Negative-compile test.  This file must fail to compile.
//
// Violation: the keyed retag into Validated with a key that code outside
// the door of the trust ladder writes.
//
// The constructor of the key is private, and the door of the ladder is its
// one friend.

#include <crucible/ValidatedOp.h>

int main() {
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.schema_hash = crucible::SchemaHash{0x1234};
                                                     return built;
                                                 }};
    const auto forged = ::fixy::retag<crucible::vessel_trust::Validated>(crucible::mint_ffi_entry(entry),
                                                                         crucible::detail::EntryLadderKey{});
    return crucible::mint_validated_op(forged).is_some() ? 0 : 1;
}
