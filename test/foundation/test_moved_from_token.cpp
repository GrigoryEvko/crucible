// A token that a move spent authorizes nothing.
//
// A move hands the authority of a token to the target.  C++ leaves the
// source a valid object, so each token of this file records that a move
// spent it, and each use of a spent token ends the process.  Each case
// below spends a token and then uses it one more time, in a child through
// expect_dies of the Check family, which reads that the use ended the child.
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

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Platform.h>

#include <cstdint>
#include <utility>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;
using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;

// The reports of a use of a spent token: a spent Capability, a writer of a
// different run of the mint site, and a moved-from writer that builds a cell.
constexpr ::foundation::core::TextView kSpentCapability{"foundation: contract violation: state_ == State::live"};
constexpr ::foundation::core::TextView kOtherWriter{
    "foundation: fatal: Atomic<T, SoleWriter<Role, Brand>>: a writer of a different run of the mint site used the "
    "cell"};
constexpr ::foundation::core::TextView kMovedFromBuilder{
    "foundation: fatal: Atomic<T, SoleWriter<Role, Brand>>: a moved-from writer built the cell"};

using AllocToken = fe::Capability<Effect::Alloc, fe::Test>;

// The token that a call gives back by reference.  The rule use-after-move
// does not read through it.
template <class Token>
[[nodiscard]] Token& held(Token& token) noexcept {
    return token;
}

// A live token consumes one time.
void live_token_consumes() noexcept {
    AllocToken token = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    std::move(token).consume();
}

// A consume of a token that a move spent ends the process.
void moved_from_consume_aborts(TestRunnerCtx const& test) noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    std::move(target).consume();
    expect_dies(test, [&source] noexcept { std::move(held(source)).consume(); }, kSpentCapability);
}

// A second consume of one token ends the process.
void second_consume_aborts(TestRunnerCtx const& test) noexcept {
    AllocToken token = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    std::move(held(token)).consume();
    expect_dies(test, [&token] noexcept { std::move(held(token)).consume(); }, kSpentCapability);
}

// A trade of a moved-from token for its bare tag ends the process.
void moved_from_extract_aborts(TestRunnerCtx const& test) noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    std::move(target).consume();
    expect_dies(
        test, [&source] noexcept { static_cast<void>(fe::extract_bare(std::move(held(source)))); }, kSpentCapability);
}

// A move of a spent token gives its authority to no target, so the move
// ends the process.  A spent token then passes to no function by value.
void moved_from_move_aborts(TestRunnerCtx const& test) noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    std::move(target).consume();
    expect_dies(
        test,
        [&source] noexcept {
            AllocToken second{std::move(held(source))};
            std::move(second).consume();
        },
        kSpentCapability);
}

// A move assignment from a spent token ends the process too.  The live
// target then ends with no consume.
void moved_from_assignment_aborts(TestRunnerCtx const& test) noexcept {
    AllocToken source = fe::mint_cap<Effect::Alloc>(fe::testing::test());
    AllocToken target{std::move(held(source))};
    expect_dies(test, [&source, &target] noexcept { target = std::move(held(source)); }, kSpentCapability);
}

// The role of the cell of the writer cases.
struct RingHead {};

// A moved-from writer authorizes no store, no load of the writer and no
// build of a cell.  The target of the move authorizes each of them.  A
// writer is linear, so each body of a death test takes a Ref of a writer by
// value, no closure captures a writer, and each writer ends at the drop door.
void moved_from_writer_aborts(TestRunnerCtx const& test) noexcept {
    auto source = ::foundation::core::mint_sole_writer<RingHead>();
    using Writer = decltype(source);
    using Cell = ::foundation::core::Atomic<std::uint64_t, Writer>;
    Cell cell{source, 5};
    Writer target{std::move(held(source))};
    cell.store_release(target, 6);
    auto const spent = ::foundation::core::to(source);
    cell.store_release(target, 7);
    expect_dies(test, [&cell, spent] noexcept { cell.store_release(*spent, 8); }, kOtherWriter);
    expect_dies(test, [&cell, spent] noexcept { static_cast<void>(cell.load_owned(*spent)); }, kOtherWriter);
    expect_dies(
        test,
        [spent] noexcept {
            Cell const second{*spent, 1};
            static_cast<void>(second.load_acquire());
        },
        kMovedFromBuilder);
    CRUCIBLE_FATAL_INVARIANT(cell.load_owned(target) == 7);
    ::foundation::linear::drop(std::move(source));
    ::foundation::linear::drop(std::move(target));
}

// A move assignment spends its source too.  The target ends at the drop door
// first, so the assignment drops no live writer.
void assigned_from_writer_aborts(TestRunnerCtx const& test) noexcept {
    auto source = ::foundation::core::mint_sole_writer<RingHead>();
    using Writer = decltype(source);
    ::foundation::core::Atomic<std::uint64_t, Writer> cell{source, 5};
    Writer target{std::move(held(source))};
    Writer moved{std::move(held(target))};
    ::foundation::linear::drop(std::move(target));
    target = std::move(held(moved));
    auto const spent = ::foundation::core::to(moved);
    expect_dies(test, [&cell, spent] noexcept { cell.store_release(*spent, 9); }, kOtherWriter);
    CRUCIBLE_FATAL_INVARIANT(cell.load_owned(target) == 5);
    ::foundation::linear::drop(std::move(source));
    ::foundation::linear::drop(std::move(target));
    ::foundation::linear::drop(std::move(moved));
}

}  // namespace

int main() {
    TestRunnerCtx const test{fe::testing::test()};
    live_token_consumes();
    moved_from_consume_aborts(test);
    second_consume_aborts(test);
    moved_from_extract_aborts(test);
    moved_from_move_aborts(test);
    moved_from_assignment_aborts(test);
    moved_from_writer_aborts(test);
    assigned_from_writer_aborts(test);
    return 0;
}
