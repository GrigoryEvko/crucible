// The witnessed deref: a borrow that names what has to be presented
// before it can be read.
//
// fixy/Borrowed.h ties a borrow to an owner tag and a brand, and leaves
// WHEN it may be read to lexical scope.  That is the whole of what Rust
// gives.  The cells below stand on the two obligations this tree can
// state instead: a protocol position, and an effect row.
//
// Each cell is a pair.  The read that is admitted is shown to reach the
// bytes, and the neighbouring read that is refused is shown to be
// refused, because a gate that admitted everything would pass the first
// half of every cell.

#include <fixy/session/Witnessed.h>

#include <fixy/Borrowed.h>
#include <fixy/Ctx.h>
#include <fixy/OwnedRegion.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <fixy/Core.h>
#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
namespace s = ::fixy::session;

namespace {

using ::fixy::check::pass;

// ── The two obligations, and what discharges each ────────────────────

struct Cache {
    using permission_row = eff::Row<>;
};
struct Spilled {
    using permission_row = eff::Row<eff::Effect::IO>;
};

using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

struct Ping {
    int value = 0;
};
struct Wire {
    int last_sent = 0;
};
struct OtherWire {
    int last_sent = 0;
};

using Sending = s::Send<Ping, s::End>;
using Looping = s::Loop<s::Send<Ping, s::Continue>>;

constexpr auto send_ping = [](Wire& wire, Ping& ping) noexcept {
    wire.last_sent = ping.value;
    return true;
};

// The bytes that each protocol cell borrows.
std::uint64_t protocol_storage[3] = {5, 6, 7};

// A named brand for the type-level cells.  A borrow that names no brand
// is DefaultBrand, which the rule erased-brand counts and
// does not let a new file add; every claim below reads the same at
// either brand.  The runtime cells mint their borrows, so they carry a
// fresh brand and never spell this one.
struct probe_brand {};

// The question every cell asks of the gate, written once.
template <typename Wit, typename Presented>
concept CanDeref = requires(Wit const& witnessed, Presented const& presented) { deref(witnessed, presented); };

// A requires-expression in a non-template context is checked where it
// is written rather than answered, so the accessor question lives in a
// concept too.
template <typename Wit>
concept HasBareAccessor = requires(Wit const& w) { w.size(); } || requires(Wit const& w) { w.data(); }
                       || requires(Wit const& w) { w[std::size_t{0}]; };

// ── EFFECT ───────────────────────────────────────────────────────────

void test_a_row_witnessed_borrow_reads_under_a_context_that_admits_it(::fixy::TestRunnerCtx const& test) {
    static std::uint64_t storage[4] = {2, 4, 6, 8};
    FgCtx ctx = eff::testing::foreground();

    auto region = ::fixy::mint_owned_region(storage, std::size_t{4}, perm::mint_permission_root<Cache>());
    auto borrow = ::fixy::mint_borrowed(region);
    auto witnessed = ::fixy::session::mint_witnessed_under(borrow);

    // The obligation is the tag's, read off the borrow rather than
    // spelled by the caller, so there is nothing here to spell wrongly.
    static_assert(std::is_same_v<decltype(witnessed)::witness_type, ::fixy::session::witness::UnderRow<Cache>>);

    auto read = deref(witnessed, ctx);
    CRUCIBLE_TEST_ASSERT(test, read.size() == 4);
    CRUCIBLE_TEST_ASSERT(test, read[0] == 2);
    CRUCIBLE_TEST_ASSERT(test, read[3] == 8);

    std::uint64_t sum = 0;
    for (std::uint64_t value : read)
        sum += value;
    CRUCIBLE_TEST_ASSERT(test, sum == 20);
}

void test_a_context_that_refuses_the_row_refuses_the_read() {
    using CacheBorrow = ::fixy::Borrowed<std::uint64_t, Cache, probe_brand>;
    using SpilledBorrow = ::fixy::Borrowed<std::uint64_t, Spilled, probe_brand>;
    using CacheWitnessed = ::fixy::session::Witnessed<CacheBorrow, ::fixy::session::witness::UnderRow<Cache>>;
    using SpilledWitnessed = ::fixy::session::Witnessed<SpilledBorrow, ::fixy::session::witness::UnderRow<Spilled>>;

    // A region whose tag costs nothing is read from anywhere.  One whose
    // tag says IO is read only where IO is permitted, and the refusal is
    // the same Subrow test a lend from the permission pool already runs.
    static_assert(CanDeref<CacheWitnessed, FgCtx>);
    static_assert(!CanDeref<SpilledWitnessed, FgCtx>, "a foreground context does not admit a spilled region");
    static_assert(CanDeref<SpilledWitnessed, IoCtx>, "and a context that carries IO does");
}

// ── TEMPORAL ─────────────────────────────────────────────────────────

void test_a_protocol_witnessed_borrow_reads_while_the_session_is_there(::fixy::TestRunnerCtx const& test) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Sending>(ctx, Wire{});
    auto borrow = ::fixy::mint_borrowed<Cache>(protocol_storage);
    auto witnessed = ::fixy::session::mint_witnessed_at(handle, borrow);

    static_assert(
        std::is_same_v<decltype(witnessed)::witness_type, ::fixy::session::witness::AtProtocol<decltype(handle)>>,
        "the witness names the full type of the handle");

    auto read = deref(witnessed, handle);
    CRUCIBLE_TEST_ASSERT(test, read.size() == 3);
    CRUCIBLE_TEST_ASSERT(test, read[1] == 6);

    // Advancing the session consumes the handle and hands back one at
    // the next position.  The borrow's witness names the old handle, so
    // the new handle does not discharge it.
    auto at_end = std::move(handle).send(Ping{11}, [](Wire& wire, Ping& ping) noexcept {
        wire.last_sent = ping.value;
        return true;
    });
    static_assert(!CanDeref<decltype(witnessed), decltype(at_end)>,
                  "a handle one step on is not a handle at the position the borrow was minted at");

    Wire closed = std::move(at_end).close();
    CRUCIBLE_TEST_ASSERT(test, closed.last_sent == 11);
}

void test_the_position_is_the_witness_and_not_the_spelling() {
    using AtSend = s::SessionHandle<Sending, Wire>;
    using AtEnd = s::SessionHandle<s::End, Wire>;
    using AtSendOtherWire = s::SessionHandle<Sending, OtherWire>;
    using SendBorrow = ::fixy::Borrowed<std::uint64_t, Cache, probe_brand>;
    using Witnessed = ::fixy::session::Witnessed<SendBorrow, ::fixy::session::witness::AtProtocol<AtSend>>;

    static_assert(CanDeref<Witnessed, AtSend>);
    static_assert(!CanDeref<Witnessed, AtEnd>, "the same handle spelling at another position is another witness");
    static_assert(!CanDeref<Witnessed, AtSendOtherWire>, "a session over another Resource is another session");

    // The handle being linear is what makes that refusal worth
    // something: a copy would leave a witness behind at every step the
    // session took.
    static_assert(!std::is_copy_constructible_v<AtSend>);
    static_assert(!std::is_copy_assignable_v<AtSend>);
}

// ── TEMPORAL at run time ─────────────────────────────────────────────
//
// Two handles of one type pass the gate of the type: two sessions of one
// shape, or one session before and after a loop iteration.  The carrier
// keeps the time of the handle of the mint, and a read against a handle
// of another time ends the process with Witness_Stale.  Each such read runs
// in a child through expect_dies of the Check family.

constexpr ::fixy::TextView kStale{"fixy::session: diagnostic [Witness_Stale]"};

void test_a_witness_refuses_another_session_of_the_same_type(::fixy::TestRunnerCtx const& test) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto first = s::mint_session<Sending>(ctx, Wire{});
    auto second = s::mint_session<Sending>(ctx, Wire{});
    static_assert(std::is_same_v<decltype(first), decltype(second)>);
    auto witnessed = ::fixy::session::mint_witnessed_at(first, ::fixy::mint_borrowed<Cache>(protocol_storage));

    CRUCIBLE_TEST_ASSERT(test, deref(witnessed, first).size() == 3);
    ::fixy::check::expect_dies(
        test, [&witnessed, &second] noexcept { static_cast<void>(deref(witnessed, second)); }, kStale);

    static_cast<void>(std::move(first).send(Ping{1}, send_ping).close());
    static_cast<void>(std::move(second).send(Ping{2}, send_ping).close());
}

void test_a_witness_refuses_the_same_session_after_a_loop_iteration(::fixy::TestRunnerCtx const& test) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<Looping>(ctx, Wire{});
    auto witnessed = ::fixy::session::mint_witnessed_at(head, ::fixy::mint_borrowed<Cache>(protocol_storage));
    CRUCIBLE_TEST_ASSERT(test, deref(witnessed, head).size() == 3);

    head = std::move(head).send(Ping{3}, send_ping);
    static_assert(CanDeref<decltype(witnessed), decltype(head)>, "a loop iteration gives the handle its type again");
    ::fixy::check::expect_dies(
        test, [&witnessed, &head] noexcept { static_cast<void>(deref(witnessed, head)); }, kStale);

    std::move(head).detach(s::detach_reason::InfiniteLoopProtocol{});
}

// A move is not a step, so the witness reads against the handle that the
// move filled.
void test_a_witness_follows_a_move_of_the_handle(::fixy::TestRunnerCtx const& test) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<Sending>(ctx, Wire{});
    auto witnessed = ::fixy::session::mint_witnessed_at(head, ::fixy::mint_borrowed<Cache>(protocol_storage));
    auto moved = std::move(head);

    CRUCIBLE_TEST_ASSERT(test, deref(witnessed, moved).size() == 3);

    static_cast<void>(std::move(moved).send(Ping{4}, send_ping).close());
}

// A session that ends releases its record, and the next session of the
// same type can claim the same record.  The claim gives the record a new
// generation, so the witness of the first session refuses the second.
void test_a_witness_refuses_a_later_session_on_the_same_record(::fixy::TestRunnerCtx const& test) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto first = s::mint_session<Sending>(ctx, Wire{});
    const s::watch::endpoint_id record = first.watch_endpoint();
    auto witnessed = ::fixy::session::mint_witnessed_at(first, ::fixy::mint_borrowed<Cache>(protocol_storage));
    static_cast<void>(std::move(first).send(Ping{5}, send_ping).close());

    auto second = s::mint_session<Sending>(ctx, Wire{});
    CRUCIBLE_TEST_ASSERT(test, second.watch_endpoint() == record);
    CRUCIBLE_TEST_ASSERT(test, second.step_count() == 1);
    ::fixy::check::expect_dies(
        test, [&witnessed, &second] noexcept { static_cast<void>(deref(witnessed, second)); }, kStale);

    static_cast<void>(std::move(second).send(Ping{6}, send_ping).close());
}

// ── One gate, two kinds ──────────────────────────────────────────────

void test_a_witness_of_one_kind_is_not_the_other() {
    using CacheBorrow = ::fixy::Borrowed<std::uint64_t, Cache, probe_brand>;
    using RowWitnessed = ::fixy::session::Witnessed<CacheBorrow, ::fixy::session::witness::UnderRow<Cache>>;
    using AtSend = s::SessionHandle<Sending, Wire>;
    using PositionWitnessed = ::fixy::session::Witnessed<CacheBorrow, ::fixy::session::witness::AtProtocol<AtSend>>;

    static_assert(CanDeref<RowWitnessed, FgCtx>);
    static_assert(!CanDeref<RowWitnessed, AtSend>, "a handle does not stand in for a row");
    static_assert(CanDeref<PositionWitnessed, AtSend>);
    static_assert(!CanDeref<PositionWitnessed, FgCtx>, "a context does not stand in for a protocol position");

    // Neither carrier has an accessor of its own, so a read that skipped
    // the gate has nothing to call.
    static_assert(!HasBareAccessor<RowWitnessed>);
    static_assert(!HasBareAccessor<PositionWitnessed>);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_a_row_witnessed_borrow_reads_under_a_context_that_admits_it(test);
    pass(test, "  test_a_row_witnessed_borrow_reads_under_a_context_that_admits_it: PASSED\n");
    test_a_context_that_refuses_the_row_refuses_the_read();
    pass(test, "  test_a_context_that_refuses_the_row_refuses_the_read: PASSED\n");
    test_a_protocol_witnessed_borrow_reads_while_the_session_is_there(test);
    pass(test, "  test_a_protocol_witnessed_borrow_reads_while_the_session_is_there: PASSED\n");
    test_the_position_is_the_witness_and_not_the_spelling();
    pass(test, "  test_the_position_is_the_witness_and_not_the_spelling: PASSED\n");
    test_a_witness_refuses_another_session_of_the_same_type(test);
    pass(test, "  test_a_witness_refuses_another_session_of_the_same_type: PASSED\n");
    test_a_witness_refuses_the_same_session_after_a_loop_iteration(test);
    pass(test, "  test_a_witness_refuses_the_same_session_after_a_loop_iteration: PASSED\n");
    test_a_witness_follows_a_move_of_the_handle(test);
    pass(test, "  test_a_witness_follows_a_move_of_the_handle: PASSED\n");
    test_a_witness_refuses_a_later_session_on_the_same_record(test);
    pass(test, "  test_a_witness_refuses_a_later_session_on_the_same_record: PASSED\n");
    test_a_witness_of_one_kind_is_not_the_other();
    pass(test, "  test_a_witness_of_one_kind_is_not_the_other: PASSED\n");
    pass(test, "test_witnessed: PASSED\n");
    return 0;
}
