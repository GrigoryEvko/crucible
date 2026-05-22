// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-087 HS14 fixture #2 of 2 for mint_permission_fork's new
// type-level rejection of crucible::fixy::ctrl::throws.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// CV-REF-DECAY half — the Callable carries `ctrl::throws const&` as a
// top-level template argument.  V-087's type-tree-contains trait
// applies `std::remove_cvref_t` at every node, so the cv-qualified
// reference must NOT launder past the rejection.  The load-bearing
// static_assert in mint_permission_fork MUST still fire.
//
// Why this matters: without cv-ref decay, a caller could launder
// `ctrl::throws` past the V-087 reject by wrapping it in a const-ref
// in a template-argument position.  The V-085 (banned sync primitive)
// discipline pioneered the cv-ref pierce; V-087 inherits it.
//
// Sibling fixture
// `neg_permission_fork_callable_carrying_throws_grant_rejected.cpp`
// exercises the DIRECT-GRANT half (the trait's primary reject path,
// no cv-qualifiers).
//
// Expected diagnostic: "static assertion failed|FIXY-V-087|
// type_tree_contains_throws|crucible::fixy::ctrl::throws".

#include <crucible/permissions/PermissionFork.h>
#include <crucible/fixy/ctrl/Throws.h>

namespace neg_permission_fork_callable_carrying_cvref_throws_rejected {

struct Whole {};
struct Left {};
struct Right {};

// Templated callable carrying its `Grant` template argument verbatim
// — when Grant is a cv-qualified reference to `ctrl::throws`, the V-087
// trait MUST still detect the throws tag via cv-ref decay at the
// recursive node.  If decay were absent, the laundering would bypass
// the reject (the bug class V-085 documented as the "permits laundered
// via const&" trap).
template <typename Grant>
struct CvRefThrowingCallable {
    template <typename Perm, typename Ctx>
    void operator()(Perm, Ctx const&) const noexcept {}
};

}  // namespace neg_permission_fork_callable_carrying_cvref_throws_rejected

namespace crucible::safety {

template <>
struct splits_into_pack<
    neg_permission_fork_callable_carrying_cvref_throws_rejected::Whole,
    neg_permission_fork_callable_carrying_cvref_throws_rejected::Left,
    neg_permission_fork_callable_carrying_cvref_throws_rejected::Right>
    : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_fork_callable_carrying_cvref_throws_rejected;
    namespace eff  = ::crucible::effects;
    namespace safe = ::crucible::safety;
    namespace ctrl = ::crucible::fixy::ctrl;

    auto whole = safe::mint_permission_root<tags::Whole>();

    // STRAINING POINT: CvRefThrowingCallable<ctrl::throws const&> wraps
    // the throws tag in a `const&` qualifier at the template-argument
    // level.  The V-087 `type_tree_contains_v` trait applies
    // `std::remove_cvref_t` at every node, so the decayed tag still
    // matches — the static_assert in mint_permission_fork's body
    // MUST fire.  If this file compiles, the cv-ref pierce regressed
    // and `ctrl::throws` can be laundered past the reject by adding a
    // const-ref qualifier.
    auto rebuilt = safe::mint_permission_fork<tags::Left, tags::Right>(
        safe::PermissionForkSpawnCtx{},
        std::move(whole),
        tags::CvRefThrowingCallable<ctrl::throws const&>{},
        tags::CvRefThrowingCallable<ctrl::throws const&>{}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
