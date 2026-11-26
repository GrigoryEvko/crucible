// Negative-compile test.  This file must fail to compile.
//
// Violation: mint_validated_op with a View of records.
//
// A View has no proof that no code writes its records after the mint: the
// array of the caller keeps its value doors, and a write through them after
// the mint would change the records of the op.  The mint takes the records
// only from the one writer of the run of a frame, by move, or from a Sealed
// list.

#include <crucible/ValidatedOp.h>

int main() {
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.schema_hash = crucible::SchemaHash{0x1234};
                                                     built.num_inputs = 1;
                                                     return built;
                                                 }};
    const ::fixy::FixedArray<crucible::TensorMeta, 1> records{};
    const auto op =
        crucible::mint_validated_op(crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                    ::fixy::View<const crucible::TensorMeta>{records.view()});
    return op.is_some() ? 0 : 1;
}
