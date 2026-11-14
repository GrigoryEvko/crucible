// A token that a move spent authorizes nothing.
//
// A move hands the authority of a token to the target.  C++ leaves the
// source a valid object, so each token of this file records that a move
// spent it, and each use of a spent token ends the process.  Each case
// below spends a token and then uses it one more time, and the abort
// probe reads that the use ended the process.
//
// The static rule use-after-move refuses a use of a moved-from local.  It
// does not see a token that a member, a pointer or a reference holds, so
// each case reaches the spent token through a reference.

#include <foundation/effects/Capability.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"

#include <utility>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;

using AllocToken = fe::Capability<Effect::Alloc, fe::Test>;

// The token that a call gives back by reference.  The rule use-after-move
// does not read through it.
[[nodiscard]] AllocToken& held(AllocToken& token) noexcept { return token; }

// A live token consumes one time.
[[nodiscard]] bool live_token_consumes() noexcept {
    AllocToken token = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    return !crucible::test::aborts([&] { std::move(token).consume(); });
}

// A consume of a token that a move spent ends the process.
[[nodiscard]] bool moved_from_consume_aborts() noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    std::move(target).consume();
    return crucible::test::aborts([&] { std::move(held(source)).consume(); });
}

// A second consume of one token ends the process.
[[nodiscard]] bool second_consume_aborts() noexcept {
    AllocToken token = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    std::move(held(token)).consume();
    return crucible::test::aborts([&] { std::move(held(token)).consume(); });
}

// A trade of a moved-from token for its bare tag ends the process.
[[nodiscard]] bool moved_from_extract_aborts() noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    std::move(target).consume();
    return crucible::test::aborts([&] { static_cast<void>(fe::extract_bare(std::move(held(source)))); });
}

// A move of a spent token gives its authority to no target, so the move
// ends the process.  A spent token then passes to no function by value.
[[nodiscard]] bool moved_from_move_aborts() noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    std::move(target).consume();
    return crucible::test::aborts([&] {
        AllocToken second{std::move(held(source))};
        std::move(second).consume();
    });
}

// A move assignment from a spent token ends the process too.  The live
// target then ends with no consume.
[[nodiscard]] bool moved_from_assignment_aborts() noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    return crucible::test::aborts([&] { target = std::move(held(source)); });
}

}  // namespace

int main() {
    if (!live_token_consumes()) return 1;
    if (!moved_from_consume_aborts()) return 2;
    if (!second_consume_aborts()) return 3;
    if (!moved_from_extract_aborts()) return 4;
    if (!moved_from_move_aborts()) return 5;
    if (!moved_from_assignment_aborts()) return 6;
    return 0;
}
