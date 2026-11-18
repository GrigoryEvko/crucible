// A file with no name is made in the file system, and the open can wait in
// the kernel, so mint_temporary_file_at asks for Block beside IO.  A context
// that owns IO and not Block is refused.

#include <fixy/os/Fs.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoOnlyCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>;

[[maybe_unused]] bool opens(IoOnlyCtx const& ctx, fixy::fs::DirectoryHandle const& dir) {
    return fixy::fs::mint_temporary_file_at(ctx, dir.ref()).is_ok();
}

}  // namespace
