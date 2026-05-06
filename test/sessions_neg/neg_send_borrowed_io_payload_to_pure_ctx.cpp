// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-074: Borrowed<T, Tag> is transparent for payload-row accounting.
// Borrowing authority cannot hide an IO Computation from HotFgCtx's
// Row<> admission gate.

#include <crucible/effects/Computation.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct IoBuffer {};
struct BufferPerm {};
struct Resource {};

int main() {
    using IoValue = eff::Computation<eff::Row<eff::Effect::IO>, IoBuffer>;
    using Proto = proto::Send<proto::Borrowed<IoValue, BufferPerm>,
                              proto::End>;

    eff::HotFgCtx fg;
    [[maybe_unused]] auto handle =
        proto::mint_permissioned_session<Proto>(fg, Resource{});
    return 0;
}
