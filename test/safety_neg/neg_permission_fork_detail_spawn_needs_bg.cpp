// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// detail::permission_fork_spawn_ starts one thread per element of any
// tuple it is handed.  Any translation unit can name it, so it asserts
// that its context owns the background effect: a call under the
// foreground context is refused.
//
// Expected diagnostic: the spawn helper's static assertion names the
// background effect.

#include <crucible/permissions/_PermissionFork.h>

#include <tuple>
#include <utility>

int main() {
    namespace eff = ::crucible::effects;
    namespace safe = ::crucible::safety;

    std::tuple<int> children{0};
    auto body = [](int, eff::HotFgCtx const&) noexcept {};
    safe::detail::permission_fork_spawn_(eff::HotFgCtx{}, std::move(children), std::tuple<decltype(body)>{body},
                                         std::index_sequence<0>{});
    return 0;
}
