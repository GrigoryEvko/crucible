// Negative-compile test.  This file must fail to compile.
//
// Violation: mint_validated_op with a temporary Sealed list of records.
//
// The op keeps a View of the sealed list.  A temporary list ends at the end
// of the full expression, and the op would then read records that no object
// holds.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x1234};
    const auto op =
        crucible::mint_validated_op(crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                    crucible::SealedRecords<1>{::fixy::FixedList<crucible::TensorMeta, 1>{}});
    return op.is_some() ? 0 : 1;
}
