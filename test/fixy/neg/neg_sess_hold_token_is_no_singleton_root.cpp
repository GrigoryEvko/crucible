// A token that leaves a session hold keeps the brand of its root.  So a token
// of a root of mint_permission_root is no singleton root, and
// release_singleton_root, which takes only a token on the erased identity,
// refuses it.  The tag has a stable identity, so the brand is the one reason
// of the refusal.

#include <fixy/session/Flow.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_hold_token_is_no_singleton_root_tags {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_hold_token_is_no_singleton_root_tags

int main() {
    using neg_sess_hold_token_is_no_singleton_root_tags::Region;
    auto hold = ::fixy::session::mint_permission_hold(::foundation::permissions::mint_permission_root<Region>());
    auto [token] = std::move(hold).into_permissions();
    ::foundation::permissions::release_singleton_root(std::move(token));
    return 0;
}
