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
//
// A Capability records the spend in a byte of state, and a SoleWriter gives
// its run-time id to the target and keeps 0.  A Permission is an empty
// class, so it records no spend.  foundation/permissions/Permission.h states
// that trade-off, and the static rule use-after-move is its one check.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"

#include <cstdint>
#include <utility>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;

using AllocToken = fe::Capability<Effect::Alloc, fe::Test>;

// The token that a call gives back by reference.  The rule use-after-move
// does not read through it.
template <class Token>
[[nodiscard]] Token& held(Token& token) noexcept {
    return token;
}

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

// The role of the cell of the writer cases.
struct RingHead {};

// A moved-from writer authorizes no store, no load of the writer and no
// build of a cell.  The target of the move authorizes each of them.  A
// writer is linear, so each probe body takes a Ref of a writer by value, no
// closure captures a writer, and each writer ends at the drop door.
[[nodiscard]] bool moved_from_writer_aborts() noexcept {
    auto source = ::foundation::core::mint_sole_writer<RingHead>();
    using Writer = decltype(source);
    using Cell = ::foundation::core::Atomic<std::uint64_t, Writer>;
    Cell cell{source, 5};
    Writer target{std::move(held(source))};
    cell.store_release(target, 6);
    auto const live = ::foundation::core::to(target);
    auto const spent = ::foundation::core::to(source);
    bool const target_writes = !crucible::test::aborts([&cell, live] { cell.store_release(*live, 7); });
    bool const store_aborts = crucible::test::aborts([&cell, spent] { cell.store_release(*spent, 8); });
    bool const load_aborts = crucible::test::aborts([&cell, spent] { static_cast<void>(cell.load_owned(*spent)); });
    bool const build_aborts = crucible::test::aborts([spent] {
        Cell const second{*spent, 1};
        static_cast<void>(second.load_acquire());
    });
    bool const target_holds = cell.load_owned(target) == 7;
    ::foundation::linear::drop(std::move(source));
    ::foundation::linear::drop(std::move(target));
    return target_writes && store_aborts && load_aborts && build_aborts && target_holds;
}

// A move assignment spends its source too.  The target ends at the drop door
// first, so the assignment drops no live writer.
[[nodiscard]] bool assigned_from_writer_aborts() noexcept {
    auto source = ::foundation::core::mint_sole_writer<RingHead>();
    using Writer = decltype(source);
    ::foundation::core::Atomic<std::uint64_t, Writer> cell{source, 5};
    Writer target{std::move(held(source))};
    Writer moved{std::move(held(target))};
    ::foundation::linear::drop(std::move(target));
    target = std::move(held(moved));
    auto const spent = ::foundation::core::to(moved);
    bool const moved_aborts = crucible::test::aborts([&cell, spent] { cell.store_release(*spent, 9); });
    bool const target_holds = cell.load_owned(target) == 5;
    ::foundation::linear::drop(std::move(source));
    ::foundation::linear::drop(std::move(target));
    ::foundation::linear::drop(std::move(moved));
    return moved_aborts && target_holds;
}

}  // namespace

int main() {
    if (!live_token_consumes()) return 1;
    if (!moved_from_consume_aborts()) return 2;
    if (!second_consume_aborts()) return 3;
    if (!moved_from_extract_aborts()) return 4;
    if (!moved_from_move_aborts()) return 5;
    if (!moved_from_assignment_aborts()) return 6;
    if (!moved_from_writer_aborts()) return 7;
    if (!assigned_from_writer_aborts()) return 8;
    return 0;
}
