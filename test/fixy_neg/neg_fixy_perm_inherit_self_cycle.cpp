// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture: mint_permission_inherit via fixy::perm
// rejects circular inheritance (DeadTag listed as its own survivor).
//
// Violation: the inherit machinery explicitly forbids
//   static_assert((!std::is_same_v<DeadTag, SurvivorTags> && ...))
// so passing DeadTag as a survivor of itself is a hard compile error.
// Routing through `fixy::perm::mint_permission_inherit` must reject
// identically.
//
// Expected diagnostic: mint_permission_inherit forbids circular
// inheritance.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_inherit_self_cycle {

struct DeadTag {};

}  // namespace neg_fixy_perm_inherit_self_cycle

int main() {
    namespace tags  = neg_fixy_perm_inherit_self_cycle;
    namespace fperm = ::crucible::fixy::perm;

    // Should FAIL: DeadTag cannot inherit to itself.
    [[maybe_unused]] auto bad = fperm::mint_permission_inherit<
        tags::DeadTag, tags::DeadTag>();
    return 0;
}
