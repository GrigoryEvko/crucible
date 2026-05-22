// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-087 HS14 fixture #1 of 2 for mint_permission_fork's new
// type-level rejection of crucible::fixy::ctrl::throws.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// DIRECT-GRANT half — the Callable carries `ctrl::throws` as a top-level
// template argument.  V-087's load-bearing static_assert in
// mint_permission_fork (after type-tree introspection via
// `type_tree_contains_throws_v`) MUST fire.
//
// Why this matters: a callable wrapper that EXPLICITLY engages the
// throws grant (via a template arg) is permitted to throw at runtime.
// Under `-fno-exceptions` the throw is rewritten to `std::abort`;
// under `-fexceptions` it tears through the noexcept boundary into
// `std::terminate`.  Either way, the structured fork-join is torn
// through, parent Permission rebuild never happens, child Permissions
// strand.  The V-087 static_assert at the fork's body — STRONGER than
// the existing `is_nothrow_invocable_v` rail — rejects this at the
// type level before any token consumption.
//
// Sibling fixture
// `neg_permission_fork_callable_carrying_cvref_throws_rejected.cpp`
// exercises the CV-REF-DECAY half (the trait's cv-ref pierce
// discipline).
//
// Expected diagnostic: "static assertion failed|FIXY-V-087|
// type_tree_contains_throws|crucible::fixy::ctrl::throws".

#include <crucible/permissions/PermissionFork.h>
#include <crucible/fixy/ctrl/Throws.h>

namespace neg_permission_fork_callable_carrying_throws_grant_rejected {

struct Whole {};
struct Left {};
struct Right {};

// A NAMED callable wrapper whose template argument carries the
// `ctrl::throws` grant.  Operator() is declared `noexcept` so the
// SYNTACTIC `is_nothrow_invocable_v` rail in mint_permission_fork
// PASSES — the load-bearing reject must be V-087's TYPE-LEVEL
// `type_tree_contains_throws_v` static_assert, not the noexcept-
// invocable check.
template <typename Grant>
struct ThrowingCallable {
    template <typename Perm, typename Ctx>
    void operator()(Perm, Ctx const&) const noexcept {
        // Body is empty — V-087 reject is at the TYPE level, never
        // reaches runtime regardless.
    }
};

}  // namespace neg_permission_fork_callable_carrying_throws_grant_rejected

namespace crucible::safety {

template <>
struct splits_into_pack<
    neg_permission_fork_callable_carrying_throws_grant_rejected::Whole,
    neg_permission_fork_callable_carrying_throws_grant_rejected::Left,
    neg_permission_fork_callable_carrying_throws_grant_rejected::Right>
    : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_fork_callable_carrying_throws_grant_rejected;
    namespace eff  = ::crucible::effects;
    namespace safe = ::crucible::safety;
    namespace ctrl = ::crucible::fixy::ctrl;

    auto whole = safe::mint_permission_root<tags::Whole>();

    // STRAINING POINT: ThrowingCallable<ctrl::throws> has `ctrl::throws`
    // in its template argument list — V-087's
    // `type_tree_contains_throws_v` returns true on it, the new
    // static_assert in mint_permission_fork fires, the build reddens.
    //
    // If this file compiles, V-087's type-level reject regressed.
    auto rebuilt = safe::mint_permission_fork<tags::Left, tags::Right>(
        safe::PermissionForkSpawnCtx{},
        std::move(whole),
        tags::ThrowingCallable<ctrl::throws>{},
        tags::ThrowingCallable<ctrl::throws>{}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
