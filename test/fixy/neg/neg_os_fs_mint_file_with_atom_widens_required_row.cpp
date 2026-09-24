// An `atom::with<Es...>` in an os mint pack widens the row the context
// has to admit, and a context that does not admit the widened row is
// refused.
//
// This is the second half of the change that gave with<Es...> its lift.
// The first half is the context-admission direction, covered by
// neg_role_io_row_under_foreground_ctx: a binding that declared its
// effects is now refused by a context that admits nothing.  This is the
// direction that follows from the same mechanism and points the other
// way — the os mints fold a pack into a required row through
// foundation/effects/Lift.h, and with<Es...> now reaches that fold.
//
// Before the lift the pack below did not fail here.  It failed one
// conjunct earlier, at CtxAdmitsAtomRow's `(LiftsToRow<Atoms> && ...)`,
// because with<Es...> declared no `lifts_to` and so could not enter an
// os pack at all.  The refusal moving from "this atom cannot be folded"
// to "the folded row exceeds what the context holds" is the whole of
// what this fixture witnesses, and it is a tightening: the pack is
// admitted further and then judged on what it actually asks for.
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
    eff::ExecCtx<eff::Test,
                 eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

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
