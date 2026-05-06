// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-074: Returned<T, Tag> is transparent for payload-row accounting.
// Returning a permission cannot launder a Computation<Row<IO>, T> into
// a foreground pure context.

#include <crucible/effects/Computation.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct IoBuffer {};
struct BufferPerm {};
struct Resource {};

int main() {
    using IoValue = eff::Computation<eff::Row<eff::Effect::IO>, IoBuffer>;
    using Proto = proto::Send<proto::Returned<IoValue, BufferPerm>,
                              proto::End>;

    eff::HotFgCtx fg;
    auto perm = crucible::safety::mint_permission_root<BufferPerm>();
    [[maybe_unused]] auto handle =
        proto::mint_permissioned_session<Proto>(
            fg, Resource{}, std::move(perm));
    return 0;
}
