// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture: mint_permission_inherit via fixy::perm
// rejects when a SurvivorTag is not registered in
// survivor_registry<DeadTag>.
//
// fixy-A1-029: the §XXI signature-clarity refactor moved the
// inherits_from gate into `detail::validated_perm_tuple<>`,
// instantiated by the `mint_permission_inherit_t<...>` alias.
// Because H-25 added a `crash_witness_key` parameter, the call-site
// form short-circuits overload resolution before the alias is
// instantiated — so we route through the alias directly to force the
// static_assert in validated_perm_tuple.
//
// Expected diagnostic: mint_permission_inherit requires inherits_from
// / static_assert failure.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_inherit_without_specialization {

struct DeadTag {};
struct SurvivorTag {};

}  // namespace neg_fixy_perm_inherit_without_specialization

namespace fperm = ::crucible::fixy::perm;

// Forces validated_perm_tuple<DeadTag, list<SurvivorTag>> to
// instantiate through the fixy:: re-export of
// mint_permission_inherit_t.  inherits_from<DeadTag, SurvivorTag>
// defaults to false → triggers the third static_assert.
using BadType = fperm::mint_permission_inherit_t<
    neg_fixy_perm_inherit_without_specialization::DeadTag,
    neg_fixy_perm_inherit_without_specialization::SurvivorTag>;
static_cast<void>(sizeof(BadType));

int main() {
    return 0;
}
