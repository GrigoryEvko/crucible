// The name of a temporary directory holds the id of the process, which
// changes from run to run, so mint_temporary_directory asks for Nondet beside
// IO and Block.  A context that owns IO and Block and not Nondet is refused.

#include <fixy/os/Fs.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;

[[maybe_unused]] bool makes(IoBlockCtx const& ctx) { return fixy::fs::mint_temporary_directory(ctx).is_ok(); }

}  // namespace
