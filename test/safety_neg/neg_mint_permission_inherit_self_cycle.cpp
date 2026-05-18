// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-029 fixture: the §XXI signature-clarity refactor moved the
// inherits_from / self-cycle / non-empty-survivor gates from the
// function-body static_asserts into `detail::validated_perm_tuple<>`.
// To trigger the static_asserts after the H-25 witness parameter
// addition, this fixture instantiates the
// `mint_permission_inherit_t<...>` alias directly — see the companion
// fixture's doc-block for the full rationale.
//
// This fixture pins the SELF-CYCLE gate: even with a user-declared
// `inherits_from<PeerTag, PeerTag>` specialization (which alone would
// satisfy the third static_assert), the self-cycle assert (second of
// three) rejects DeadTag == SurvivorTag.  Substring "forbids circular
// inheritance" pins the diagnostic to `validated_perm_tuple`'s
// no-self-cycle message.

#include <crucible/permissions/PermissionInherit.h>

namespace {

struct PeerTag {};

}  // namespace

namespace crucible::permissions {

template <>
struct inherits_from<PeerTag, PeerTag> : std::true_type {};

}  // namespace crucible::permissions

// Forces detail::validated_perm_tuple<PeerTag, list<PeerTag>> to
// instantiate.  DeadTag == SurvivorTag → triggers the second
// static_assert ("forbids circular inheritance").
using BadType = ::crucible::permissions::mint_permission_inherit_t<
    PeerTag, PeerTag>;
static_cast<void>(sizeof(BadType));
