// Negative-compile test.  This file must fail to compile.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  The context of another state's claim proves that its holder owns
// that state, not a Vigil, so the call is refused.  The op is well formed,
// so the context is the only fault.

#include <crucible/Vigil.h>
#include <foundation/effects/Ctx.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::Vigil vigil = crucible::mint_vigil(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, {});
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed,
                                                 []() noexcept { return crucible::TraceRing::Entry{}; }};
    const crucible::ValidatedOp op =
        crucible::mint_validated_op(crucible::mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>())
            .expect("an entry with no tensors takes no records");
    (void)vigil.dispatch_op_pure(::foundation::effects::testing::foreground<Stranger>(), op);
    return 0;
}
