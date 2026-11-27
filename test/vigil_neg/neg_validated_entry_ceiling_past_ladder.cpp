// Negative-compile test.  This file must fail to compile.
//
// Violation: the ceiling door of the trust ladder with a ceiling of 65
// records of each kind.
//
// The ladder refuses an entry with more than 64 inputs or 64 outputs, so the
// ceiling door has no proof that it admits each entry of the ceiling, and its
// constraint refuses the call.

#include <crucible/ValidatedOp.h>

int main() {
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.schema_hash = crucible::SchemaHash{0x1234};
                                                     return built;
                                                 }};
    const auto validated = crucible::validate_entry_within_ceiling<65>(crucible::mint_ffi_entry(entry));
    return crucible::mint_validated_op(validated).is_some() ? 0 : 1;
}
