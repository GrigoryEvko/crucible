// The slots of a session handle at run time.
//
// A handle keeps the token of each tag of its set, the parked token of each
// LentOut and the loan of each BorrowedIn, in its slots.  Each step that
// moves a token or a loan takes it out of the slots of the sender and puts it
// into the slots of the receiver, so the caller gives the value of a marker
// and never its token.  The test walks each slot form of fixy/session/Flow.h
// through a pair of sessions on one thread: a Transferable and a Returned
// each move the token, and a Borrowed and its Released lend the region and
// give it back.  test_session_handle moves a bare Permission.  The Resource of each pair is the wire: one
// side closes with the message in it, or the test moves the message from
// the Resource of one live handle to the Resource of the other.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

using HoldsRegion = fp::PermSet<Region>;
using NoTags = fp::EmptyPermSet;

// One message at a time passes through the mailbox.  A FixedList of one
// element holds a linear message, which an Option cannot take by assignment.
template <class Message>
struct Mailbox {
    ::fixy::FixedList<Message, 1> held{};
    int value = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// A trying write that puts the message into an empty mailbox.
template <class Message>
[[nodiscard]] bool put_message(Mailbox<Message>& box, Message& message) noexcept {
    if (box.held.size() != 0) return false;
    static_cast<void>(box.held.append(std::move(message)).expect("the mailbox is empty"));
    return true;
}

// A polling read that takes the message out of the mailbox.
template <class Message>
[[nodiscard]] ::fixy::Option<Message> take_message(Mailbox<Message>& box) noexcept {
    return box.held.take_last();
}

// Moves the message out of the mailbox of one side into the mailbox of the
// other side.
template <class Message>
void carry(Mailbox<Message>& from, Mailbox<Message>& to) noexcept {
    ::fixy::Option<Message> taken = from.held.take_last();
    static_cast<void>(to.held.append(std::move(taken).expect("the sender put the message")).expect("empty"));
}

[[nodiscard]] int fail(::fixy::TextView what) noexcept {
    ::fixy::report(::fixy::Sink::Err, "test_session_slot_forms: {}\n", what);
    return 1;
}

// ── A token moves in a marker ────────────────────────────────────────
//
// The sender gives the value of the marker, and its End handle holds no
// tag.  The receiver gets the value of the marker, and its End handle gives
// the token back at close().
template <class Message>
[[nodiscard]] int move_in_a_marker(int value) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto sender = s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, Mailbox<Message>{},
                                                                         fp::mint_permission_root<Region>());
    static_assert(std::is_same_v<typename decltype(sender)::perm_set, HoldsRegion>);
    auto sender_done = std::move(sender).send(value, put_message<Message>);
    static_assert(std::is_same_v<typename decltype(sender_done)::perm_set, NoTags>,
                  "the sender gave the token of the slots away");
    Mailbox<Message> wire = std::move(sender_done).close();

    auto receiver = s::mint_session<s::Recv<Message, s::End>>(ctx, std::move(wire));
    auto [received, receiver_done] = std::move(receiver).recv(take_message<Message>);
    static_assert(std::is_same_v<decltype(received), int>, "the receive gives the value of the marker");
    static_assert(std::is_same_v<typename decltype(receiver_done)::perm_set, HoldsRegion>);
    auto [back, token] = std::move(receiver_done).close();
    const bool is_mailbox_empty = back.held.size() == 0;
    ::foundation::linear::drop(std::move(token));
    ::foundation::linear::drop(std::move(back));
    if (received != value) return fail("the marker did not carry its value");
    if (!is_mailbox_empty) return fail("the receive left the marker in the mailbox");
    return 0;
}

// ── A loan and its release ───────────────────────────────────────────
//
// The lender lends the region and waits for the release.  The borrower
// receives the loan into its slots, reads the region through the handle,
// and sends the release, which carries the loan back.  The lender then holds
// the token again, and its End handle gives it back.
using Lend = s::Borrowed<int, Region>;
using Release = s::Released<int, Region>;
using Lends = s::Send<Lend, s::Recv<Release, s::End>>;
using Borrows = s::Recv<Lend, s::Send<Release, s::End>>;

struct LoanWire {
    Mailbox<Lend> lent{};
    Mailbox<Release> released{};
    int region_value = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

[[nodiscard]] int lend_and_release() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto lender =
        s::mint_permissioned_session<Lends>(ctx, LoanWire{.region_value = 40}, fp::mint_permission_root<Region>());
    auto lender_waits =
        std::move(lender).send(1, [](LoanWire& wire, Lend& lend) noexcept { return put_message(wire.lent, lend); });
    static_assert(fp::perm_set_equal(^^typename decltype(lender_waits)::perm_set, ^^fp::PermSet<s::LentOut<Region>>),
                  "the token of the lender is parked in the LentOut slot");

    auto borrower = s::mint_session<Borrows>(ctx, LoanWire{});
    carry(lender_waits.resource().lent, borrower.resource().lent);
    auto [ticket, borrowing] =
        std::move(borrower).recv([](LoanWire& wire) noexcept { return take_message(wire.lent); });
    static_assert(fp::perm_set_equal(^^typename decltype(borrowing)::perm_set, ^^fp::PermSet<s::BorrowedIn<Region>>));
    const int region_value = lender_waits.resource().region_value;
    auto [seen, reading] =
        std::move(borrowing).template read<Region>([region_value](auto const&) noexcept { return region_value; });
    auto borrower_done = std::move(reading).send(
        seen + ticket, [](LoanWire& wire, Release& release) noexcept { return put_message(wire.released, release); });
    static_assert(std::is_same_v<typename decltype(borrower_done)::perm_set, NoTags>,
                  "the release carries the loan back out of the slots of the borrower");

    carry(borrower_done.resource().released, lender_waits.resource().released);
    auto [answer, lender_done] =
        std::move(lender_waits).recv([](LoanWire& wire) noexcept { return take_message(wire.released); });
    static_assert(std::is_same_v<typename decltype(lender_done)::perm_set, HoldsRegion>,
                  "the release unparks the token of the lender");
    ::foundation::linear::drop(std::move(borrower_done).close());
    auto [wire, token] = std::move(lender_done).close();
    ::foundation::linear::drop(std::move(token));
    ::foundation::linear::drop(std::move(wire));
    if (answer != 41) return fail("the release did not carry the value that the borrower read");
    return 0;
}

// ── The slots take no byte ───────────────────────────────────────────
template <class PS, class Policy>
using SendsPing = s::Handle<s::Send<int, s::End>, PS, Mailbox<int>, void, Policy>;

static_assert(sizeof(SendsPing<HoldsRegion, s::check::Off>) == sizeof(SendsPing<NoTags, s::check::Off>));
static_assert(sizeof(SendsPing<HoldsRegion, s::check::Enforced>) == sizeof(SendsPing<NoTags, s::check::Enforced>));
static_assert(sizeof(SendsPing<fp::PermSet<s::LentOut<Region>>, s::check::Enforced>)
              == sizeof(SendsPing<NoTags, s::check::Enforced>));
static_assert(sizeof(SendsPing<fp::PermSet<s::BorrowedIn<Region>>, s::check::Off>)
              == sizeof(SendsPing<NoTags, s::check::Off>));

}  // namespace

int main() {
    if (const int rc = move_in_a_marker<s::Transferable<int, Region>>(7); rc != 0) return rc;
    if (const int rc = move_in_a_marker<s::Returned<int, Region>>(9); rc != 0) return rc;
    if (const int rc = lend_and_release(); rc != 0) return rc;
    ::fixy::check::pass(kTest, "test_session_slot_forms: OK\n");
    return 0;
}
