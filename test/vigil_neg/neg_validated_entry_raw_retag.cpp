// Negative-compile test.  This file must fail to compile.
//
// Violation: the member retag of the tag of the FFI into Validated.
//
// Validated is a keyed tag, so no edge of the catalog enters it.  Only the
// door of the trust ladder (validate_entry) gives an entry the tag.

#include <crucible/ValidatedOp.h>

int main() {
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.schema_hash = crucible::SchemaHash{0x1234};
                                                     return built;
                                                 }};
    const auto forged = crucible::mint_ffi_entry(entry).retag<crucible::vessel_trust::Validated>();
    return crucible::mint_validated_op(forged).is_some() ? 0 : 1;
}
