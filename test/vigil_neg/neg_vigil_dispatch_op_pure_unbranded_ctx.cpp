// Negative-compile test.  This file must fail to compile.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  The unbranded foreground context has the empty row too, but it
// names no claim, so the call is refused.  The op is well formed, so the
// context is the only fault.

#include <crucible/Vigil.h>
#include <foundation/effects/Ctx.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing::Entry entry{};
    const crucible::ValidatedOp op =
        crucible::mint_validated_op(crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>(),
                                    ::fixy::View<const crucible::TensorMeta>{})
            .expect("an entry with no tensors takes no records");
    (void)vigil.dispatch_op_pure(::foundation::effects::testing::foreground(), op);
    return 0;
}
