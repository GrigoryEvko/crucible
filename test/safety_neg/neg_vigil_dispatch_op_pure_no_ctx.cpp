// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim as its first argument.  A call without a context is refused.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta meta{};
    (void)vigil.dispatch_op_pure(crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                 &meta, 1);
    return 0;
}
