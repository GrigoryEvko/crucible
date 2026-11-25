// The mint of sealed memory takes each page of the memory, which can wait in
// the kernel, so it asks for Block beside IO.  A context that owns IO and not
// Block is refused.

#include <fixy/os/Fs.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoOnlyCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>;

[[maybe_unused]] bool mints(IoOnlyCtx const& ctx) {
    return fixy::fs::mint_sealed_memory(ctx, fixy::fs::FileOffset{4096}).is_ok();
}

}  // namespace
