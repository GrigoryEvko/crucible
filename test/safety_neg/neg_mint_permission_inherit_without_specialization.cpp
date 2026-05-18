// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-029 fixture: the §XXI signature-clarity refactor moved the
// inherits_from / self-cycle / non-empty-survivor gates from the
// function-body static_asserts into `detail::validated_perm_tuple<>`,
// which is instantiated by the new `mint_permission_inherit_t<...>`
// trailing-return-type alias.  Because the H-25 refactor added a
// `crash_witness_key` parameter, calling the function with zero args
// short-circuits overload resolution BEFORE the return-type alias is
// instantiated — so the static_asserts never fire if we route through
// the call expression.
//
// To exercise the gate, instantiate the alias DIRECTLY: that forces
// `validated_perm_tuple<DeadTag, inheritance_list<SurvivorTag>>::type`
// to be computed, which runs the three static_asserts.  This fixture
// targets the "every survivor must have an inherits_from<DeadTag, S>
// specialization" gate — neither `DeadTag` nor `SurvivorTag` have a
// recovery edge declared, so the static_assert fires with the
// "mint_permission_inherit requires inherits_from" message.

#include <crucible/permissions/PermissionInherit.h>

namespace {

struct DeadTag {};
struct SurvivorTag {};

}  // namespace

// Forces detail::validated_perm_tuple<DeadTag, list<SurvivorTag>> to
// instantiate.  The inherits_from<DeadTag, SurvivorTag> default is
// false → triggers the third static_assert.
using BadType = ::crucible::permissions::mint_permission_inherit_t<
    DeadTag, SurvivorTag>;
static_cast<void>(sizeof(BadType));
