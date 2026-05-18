// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-009 HS14 fixture #2: the passkey chokepoint.
//
// The post-fix surface is:
//   namespace crucible::safety::detail {
//     class  ForkRebuildKey;            // private default ctor
//     struct ForkRebuildAccess {        // rebuild<T>(ForkRebuildKey)
//       template <typename T>
//       static constexpr Permission<T> rebuild(ForkRebuildKey) noexcept;
//     };
//   }
//
// Calling `ForkRebuildAccess::rebuild<T>(...)` requires a
// `ForkRebuildKey` instance, but `ForkRebuildKey`'s default
// constructor is `private` and the friend list contains only
// `detail::rebuild_parent_after_fork_`.  A user TU outside the friend
// list cannot construct the key, so cannot reach the rebuild surface
// even if it imports `ForkRebuildAccess` directly.
//
// VIOLATION: a user TU attempts to instantiate `ForkRebuildKey{}` and
// pass it through `ForkRebuildAccess::rebuild<...>`.  The private
// default constructor blocks construction at the type-system level.
//
// This fixture is structurally distinct from the free-function
// fixture above: it proves the chokepoint is real even when the user
// has read the implementation and tries to forge the key directly,
// not just call the removed legacy name.
//
// Expected diagnostic: "is private within this context", "constructor
// is private", "cannot access private member", or equivalent — any
// signal that `ForkRebuildKey{}` is unreachable from outside the
// friend list.

#include <crucible/permissions/Permission.h>

namespace neg_fork_rebuild_key_private_ctor {

struct LeakedTag {};

}  // namespace neg_fork_rebuild_key_private_ctor

int main() {
    using neg_fork_rebuild_key_private_ctor::LeakedTag;
    namespace safe = ::crucible::safety;

    // VIOLATION: ForkRebuildKey's default ctor is private — only
    // `detail::rebuild_parent_after_fork_` is friended.  A user TU
    // cannot construct the key, so cannot reach the rebuild surface.
    auto smuggled = safe::detail::ForkRebuildAccess::rebuild<LeakedTag>(
        safe::detail::ForkRebuildKey{});
    safe::permission_drop(std::move(smuggled));
    return 0;
}
