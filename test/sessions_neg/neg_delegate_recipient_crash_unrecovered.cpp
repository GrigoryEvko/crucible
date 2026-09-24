// GAPS-047: Delegate<T, K> must account for recipient-side crash
// propagation.  A delegated protocol that can emit before finishing
// requires the carrier continuation K to expose a
// Recv<Crash<Recipient>, _> recovery branch.  This fixture deliberately
// omits that branch; assert_every_offer_has_crash_branch_for must fail.

#include <crucible/sessions/SessionDelegate.h>

using namespace crucible::safety::proto;

namespace {

struct Recipient {};
struct Msg {};

using BadDelegate = Delegate<Send<Msg, End>, End>;

consteval bool probe() {
    assert_every_offer_has_crash_branch_for<BadDelegate, Recipient>();
    return true;
}

static_assert(probe());

}  // namespace
