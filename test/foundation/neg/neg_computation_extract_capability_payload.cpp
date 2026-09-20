// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 2 for the authority relation in
// foundation/effects/Computation.h.
//
// The hole this fixture stands over.  extract_admits_payload had a
// primary of std::true_type, with one specialization for a nested
// Computation hiding an engaged row.  A capability is a plain payload by
// that test.  So Computation<Row<>, Capability<Effect::IO, Bg>>, a value
// typed PURE and naming no effect at all, admitted extract and handed
// the caller an IO capability.  Every gate downstream that reads a row
// to decide what a value may do inherited that.
//
// The relation is now stated positively and enumerates what conveys
// authority, so a capability payload refuses.  extract carries
// `requires(row_size_v<R> == 0) && detail::extract_admits_payload_v<T>`,
// and the second conjunct is the one that fails here.  The row IS empty,
// which is exactly what made the old form dangerous.
//
// Fixture 2 covers a permission token, which is a different authority
// kind reaching the relation through a different specialization.  The
// admitting direction, that a plain payload still extracts, is pinned in
// Computation.h itself, because a repair that refused every payload
// would satisfy this fixture and be useless.

#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace fe = ::foundation::effects;

using IoCap = fe::Capability<fe::Effect::IO, fe::Bg>;

// A carrier whose own row is empty.  Nothing in the type says the value
// inside is an authority.
using PureOverCapability = fe::Computation<fe::Row<>, IoCap>;

int main() {
    auto bg = fe::testing::bg();
    IoCap cap = fe::mint_cap<fe::Effect::IO>(bg);
    // A capability is move-only, so the carrier takes it by move.  Doing
    // this correctly matters: a fixture that also failed here would pass
    // its gate without ever reaching the line it exists to witness.
    PureOverCapability pure = PureOverCapability::mint_computation(std::move(cap));

    // THE LOAD-BEARING LINE: must FAIL to compile.  A pure-typed carrier
    // must not hand out the authority it is carrying.
    auto escaped = pure.extract();
    (void)escaped;
    return 0;
}
