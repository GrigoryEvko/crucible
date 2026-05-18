// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-009 HS14 fixture #1: the previously-public free function
// `safety::permission_fork_rebuild_<T>()` was removed in this fix.
// Its doc-comment used to say "NOT a public API; users must go through
// mint_permission_fork", but the symbol lived at namespace scope with
// no friend/passkey gate — anyone who read the doc-comment but ignored
// it could call it directly and synthesize a fresh `Permission<T>`
// from thin air, defeating CSL linearity at zero compile-time cost.
//
// fixy-A1-009 replaces the surface with a passkey-gated chokepoint:
//   `detail::ForkRebuildAccess::rebuild<T>(detail::ForkRebuildKey{})`
// where `ForkRebuildKey`'s default constructor is private and the
// friend list is limited to the single legitimate caller
// `detail::rebuild_parent_after_fork_`.  The free-function entrypoint
// no longer exists.
//
// VIOLATION: a user TU outside the friend list tries to call the
// removed free function by its old name.  The unqualified lookup
// finds no symbol; the qualified lookup `crucible::safety::
// permission_fork_rebuild_<...>` is equally a name-not-found.
//
// Expected diagnostic: "has not been declared", "no member named
// 'permission_fork_rebuild_'", "was not declared in this scope", or
// equivalent — anything that proves the user-callable symbol is gone.

#include <crucible/permissions/Permission.h>

namespace neg_fork_rebuild_free_function {

struct LeakedTag {};

}  // namespace neg_fork_rebuild_free_function

namespace crucible::safety {

template <>
struct splits_into_pack<
    neg_fork_rebuild_free_function::LeakedTag,
    neg_fork_rebuild_free_function::LeakedTag>
    : std::true_type {};

}  // namespace crucible::safety

int main() {
    using neg_fork_rebuild_free_function::LeakedTag;
    namespace safe = ::crucible::safety;

    // VIOLATION: the free function `permission_fork_rebuild_` was
    // removed.  Pre-fix this synthesised a `Permission<LeakedTag>`
    // from outside any structured-join scope, smuggling exclusive
    // ownership of a region the caller never owned.
    auto smuggled = safe::permission_fork_rebuild_<LeakedTag>();
    safe::permission_drop(std::move(smuggled));
    return 0;
}
