// The mint of sealed memory makes a memory file, which is IO.  A context that
// owns Block and not IO is refused.

#include <fixy/os/Fs.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using BlockOnlyCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::Block>>;

[[maybe_unused]] bool mints(BlockOnlyCtx const& ctx) {
    return fixy::fs::mint_sealed_memory(ctx, fixy::fs::FileOffset{4096}).is_ok();
}

}  // namespace
