// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol receives a token of a region whose tag declares no
// permission row.  No program can mint a token for such a tag, and the
// gate cannot say which effects a touch of the region incurs.  So the
// context gate of mint_session stops the build with a diagnostic that
// names the region and the fix.
//
// Expected diagnostic: [Delivered_Region_Without_Row].

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace region_without_row_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Wire {};
struct BareRegion {};
using Proto = s::Recv<s::Transferable<int, BareRegion>, s::End>;
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
}  // namespace region_without_row_fixture

int main() {
    using namespace region_without_row_fixture;
    const BgCtx ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
