// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture: mint_permission_inherit via fixy::perm
// rejects circular inheritance (DeadTag listed as its own survivor).
//
// fixy-A1-029: the §XXI signature-clarity refactor moved the
// no-self-cycle gate into `detail::validated_perm_tuple<>`, instantiated
// by the `mint_permission_inherit_t<...>` trailing-return-type alias.
// Because H-25 added a `crash_witness_key` parameter, calling the
// function with zero args short-circuits overload resolution BEFORE
// the return-type alias is instantiated — so we route through the
// alias directly via `mint_permission_inherit_t<...>` to force
// validated_perm_tuple's static_asserts to fire.
//
// Expected diagnostic: mint_permission_inherit forbids circular
// inheritance.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_inherit_self_cycle {

struct DeadTag {};

}  // namespace neg_fixy_perm_inherit_self_cycle

namespace fperm = ::crucible::fixy::perm;

// Forces validated_perm_tuple<DeadTag, list<DeadTag>> to instantiate
// through the fixy:: re-export of mint_permission_inherit_t.
using BadType = fperm::mint_permission_inherit_t<
    neg_fixy_perm_inherit_self_cycle::DeadTag,
    neg_fixy_perm_inherit_self_cycle::DeadTag>;
static_cast<void>(sizeof(BadType));

int main() {
    return 0;
}
