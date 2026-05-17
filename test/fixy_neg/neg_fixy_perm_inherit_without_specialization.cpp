// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture: mint_permission_inherit via fixy::perm
// rejects when a SurvivorTag is not registered in
// survivor_registry<DeadTag>.
//
// Violation: inherits_from defaults to false; opt-in is per-tag.
// Routing through `fixy::perm::mint_permission_inherit` must reject
// identically to the substrate fixture
// `neg_mint_permission_inherit_without_specialization`.
//
// Expected diagnostic: mint_permission_inherit requires inherits_from
// / static_assert failure.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_inherit_without_specialization {

struct DeadTag {};
struct SurvivorTag {};

}  // namespace neg_fixy_perm_inherit_without_specialization

int main() {
    namespace tags  = neg_fixy_perm_inherit_without_specialization;
    namespace fperm = ::crucible::fixy::perm;

    // Should FAIL: inherits_from<DeadTag, SurvivorTag>::value is
    // false (no specialization of survivor_registry).
    [[maybe_unused]] auto bad = fperm::mint_permission_inherit<
        tags::DeadTag, tags::SurvivorTag>();
    return 0;
}
