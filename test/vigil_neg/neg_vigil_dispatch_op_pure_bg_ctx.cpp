// Negative-compile test.  This file must fail to compile.
//
// Vigil::dispatch_op_pure asks for the context of the Vigil's producer
// claim.  A background helper holds the background load context, whose row
// admits Bg, Alloc, IO and Block, so its call is refused.  The op is well
// formed, so the context is the only fault.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    crucible::Vigil vigil = crucible::mint_vigil(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, {});
    const crucible::TraceRing::SealedEntry entry{::fixy::build_sealed, []() noexcept {
                                                     crucible::TraceRing::Entry built{};
                                                     built.schema_hash = crucible::SchemaHash{0x1234};
                                                     return built;
                                                 }};
    const crucible::ValidatedOp op =
        crucible::mint_validated_op(
            crucible::validate_entry(crucible::mint_ffi_entry(entry)).expect("the trust ladder admits the entry"))
            .expect("an entry with no tensors takes no records");
    const ::fixy::BgLoadCtx ctx{::foundation::effects::testing::bg()};
    (void)vigil.dispatch_op_pure(ctx, op);
    return 0;
}
