// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 2 for the authority relation in
// foundation/effects/Computation.h.
//
// Fixture 1 covers a capability.  This one covers the second authority
// kind, a permission token, which reaches the relation through its own
// specialization.  The two fixtures are separate because a repair that
// enumerated only capabilities would satisfy fixture 1 and leave this
// shape open: a pure-typed carrier would still hand out the authority to
// touch a region.
//
// The row here is empty, as it was in fixture 1.  A permission carries
// no effect atom, so nothing about the carrier's type says the payload
// is an authority.  The relation has to say it.
//
// The admitting direction, that a plain payload still extracts, is
// pinned in Computation.h.  Without it a blanket refusal would satisfy
// both fixtures.

#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace fe = ::foundation::effects;
namespace fp = ::foundation::permissions;

namespace {
// A pure region: the token names no effect, so the tag needs no context
// to mint from.
struct RegionTag {
    using permission_row = fe::Row<>;
};
}  // namespace

using RegionToken = fp::Permission<RegionTag>;

// A carrier typed PURE over a token that authorizes a region.
using PureOverPermission = fe::Computation<fe::Row<>, RegionToken>;

int main() {
    RegionToken token = fp::mint_permission_root<RegionTag>();
    PureOverPermission pure = PureOverPermission::mint_computation(std::move(token));

    // THE LOAD-BEARING LINE: must FAIL to compile.  A pure-typed carrier
    // must not hand out the region authority it is carrying.
    auto escaped = pure.extract();
    (void)escaped;
    return 0;
}
