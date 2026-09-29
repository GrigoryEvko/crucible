// An `atom::with<Es...>` in an os mint pack widens the row the context
// has to admit, and a context that does not admit the widened row is
// refused.
//
// with<Es...> lifts to a row, and two fixtures cover the two directions
// of that lift.  neg_role_io_row_under_foreground_ctx covers context
// admission: a binding that declared its effects is refused by a context
// that admits nothing.  This fixture covers the other direction.  The os
// mints fold a pack into a required row through
// foundation/effects/Lift.h, and with<Es...> reaches that fold.
//
// The pack below passes the LiftsToRow conjunct of CtxAdmitsAtomRow,
// because with<Es...> declares `lifts_to`.  It is refused at the next
// conjunct: the folded row exceeds what the context holds.  The pack is
// admitted into the fold and then judged on what it actually asks for.
//
// The context admits IO and Block, which is everything mint_file's own
// atoms lift to, so the mode atom alone would pass.  Only the Bg the
// with atom contributes is missing.

#include <fixy/Path.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/Fs.h>

#include <filesystem>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace atom_fs = fixy::atom::fs;
namespace src = fixy::tags::source;

namespace {

// Admits everything mint_file's own atoms lift to, and not Bg.
using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

[[maybe_unused]] fixy::Path<src::Sanitized> sanitized(const char* raw) {
    return *fixy::sanitize::path_traversal::sanitize_path_no_dotdot(
        fixy::mint_tagged<src::External>(std::filesystem::path{raw}));
}

}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto opened =
        fs::mint_file<atom_fs::mode<fs::open_mode::ReadOnly>, fixy::atom::with<eff::Effect::Bg>>(
            ctx, sanitized("/tmp/fixy-neg-fs-with-widens"));
    return 0;
}
