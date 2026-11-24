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
// the Resource of one live handle to the Resource of the other.  Each
// protocol names the brand of the root, and each token comes back with it.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Brand.h>
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

// The set that holds the token of Region with the brand Brand.
template <class Brand>
using HoldsRegion = fp::PermSet<fp::Permission<Region, Brand>>;
using NoTags = fp::EmptyPermSet;

// A brand for the checks of the layout, which mint no token.
struct LayoutBrand {};

// One message at a time passes through the mailbox.  A FixedList of one
// element holds a linear message, which an Option cannot take by assignment.
template <class Message>
struct Mailbox {
    ::fixy::FixedList<Message, 1> held{};
    int value = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// A trying write that puts the message into an empty mailbox.  The message
// is linear, so the write takes it by move and gives it back while the
// mailbox is full.
template <class Message>
struct PutMessage {
    [[nodiscard]] ::fixy::Option<Message> operator()(Mailbox<Message>& box, Message&& message) const noexcept {
        if (box.held.size() != 0) return ::fixy::Option<Message>::some(std::move(message));
        static_cast<void>(box.held.append(std::move(message)).expect("the mailbox is empty"));
        return ::fixy::none;
    }
};

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
// the token back at close(), with the brand of the root.
template <template <class, class, class> class Marker>
[[nodiscard]] int move_in_a_marker(int value) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Brand = ::foundation::brand::brand_of_t<decltype(root)>;
    using Message = Marker<int, Region, Brand>;
    auto sender = s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, Mailbox<Message>{}, std::move(root));
    static_assert(std::is_same_v<typename decltype(sender)::perm_set, HoldsRegion<Brand>>);
    auto sender_done = std::move(sender).send(value, PutMessage<Message>{});
    static_assert(std::is_same_v<typename decltype(sender_done)::perm_set, NoTags>,
                  "the sender gave the token of the slots away");
    Mailbox<Message> wire = std::move(sender_done).close();

    auto receiver = s::mint_session<s::Recv<Message, s::End>>(ctx, std::move(wire));
    auto [received, receiver_done] = std::move(receiver).recv(take_message<Message>);
    static_assert(std::is_same_v<decltype(received), int>, "the receive gives the value of the marker");
    static_assert(std::is_same_v<typename decltype(receiver_done)::perm_set, HoldsRegion<Brand>>);
    auto [back, token] = std::move(receiver_done).close();
    static_assert(std::is_same_v<decltype(token), fp::Permission<Region, Brand>>,
                  "the token leaves the session with the brand of its root");
    ::foundation::linear::drop(std::move(token));
    std::move(back.held).expect_empty("the receive takes the marker out of the mailbox");
    if (received != value) return fail("the marker did not carry its value");
    return 0;
}

// ── A write of a marker that finds no room ──────────────────────────
//
// A write of a linear message gives the message back while it has no room,
// and the handle moves the same message into the next try.  So a message
// that the box refuses some times arrives with its value and its token, and
// a stop test that reports between two tries gives it to the caller.

// A mailbox that refuses its first writes, and counts each try.
template <class Message>
struct SlowBox {
    ::fixy::FixedList<Message, 1> held{};
    int refusals = 0;
    int tries = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

template <class Message>
struct PutSlowly {
    [[nodiscard]] ::fixy::Option<Message> operator()(SlowBox<Message>& box, Message&& message) const noexcept {
        ++box.tries;
        if (box.refusals > 0) {
            --box.refusals;
            return ::fixy::Option<Message>::some(std::move(message));
        }
        static_cast<void>(box.held.append(std::move(message)).expect("the mailbox is empty"));
        return ::fixy::none;
    }
};

// The value of a marker that came back or that the box holds, and -1 for no
// marker.  The token of the marker ends here through the drop door.
template <class Message>
[[nodiscard]] int end_marker(::fixy::Option<Message> marker) noexcept {
    return std::move(marker).match(
        [](Message message) noexcept {
            const int value = message.peek();
            ::foundation::linear::drop(std::move(message));
            return value;
        },
        []() noexcept { return -1; });
}

// The box refuses the first try of the send and the first try of its wait.
// The third try takes the marker.
[[nodiscard]] int refused_marker_is_taken() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Message = s::Transferable<int, Region, ::foundation::brand::brand_of_t<decltype(root)>>;
    auto sender =
        s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, SlowBox<Message>{.refusals = 2}, std::move(root));
    auto sender_done = std::move(sender).send(11, PutSlowly<Message>{});
    static_assert(std::is_same_v<typename decltype(sender_done)::perm_set, NoTags>);
    SlowBox<Message> wire = std::move(sender_done).close();
    const int tries = wire.tries;
    const int value = end_marker<Message>(wire.held.take_last());
    std::move(wire.held).expect_empty("the line above took the marker");
    if (value != 11) return fail("a write that found no room twice did not deliver the marker with its value");
    if (tries != 3) return fail("a write that found no room twice did not call its transport three times");
    return 0;
}

// The same box, with a stop test that never reports.  The stop test runs
// before each try, and the send gives no marker back.
[[nodiscard]] int refused_marker_is_taken_with_a_stop_test() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Message = s::Transferable<int, Region, ::foundation::brand::brand_of_t<decltype(root)>>;
    auto sender =
        s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, SlowBox<Message>{.refusals = 2}, std::move(root));
    int stop_tests = 0;
    auto sent = s::HandleFactory::send(std::move(sender), 12, PutSlowly<Message>{}, [&stop_tests]() noexcept {
        ++stop_tests;
        return false;
    });
    const int kept = end_marker<Message>(std::move(sent.kept));
    SlowBox<Message> wire = std::move(sent.next).close();
    const int tries = wire.tries;
    const int value = end_marker<Message>(wire.held.take_last());
    std::move(wire.held).expect_empty("the line above took the marker");
    if (kept != -1) return fail("a send with a stop test gave back a marker that its transport took");
    if (value != 12) return fail("a write with a stop test did not deliver the refused marker with its value");
    if (tries != 3 || stop_tests != 3) return fail("a write with a stop test did not test it before each of 3 tries");
    return 0;
}

// The box has no room, and the stop test reports before the third try.  The
// marker comes back with its value, and the box holds nothing.
[[nodiscard]] int refused_marker_comes_back_at_a_stop() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Message = s::Transferable<int, Region, ::foundation::brand::brand_of_t<decltype(root)>>;
    auto sender =
        s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, SlowBox<Message>{.refusals = 100}, std::move(root));
    int stop_tests = 0;
    auto sent = s::HandleFactory::send(std::move(sender), 13, PutSlowly<Message>{},
                                       [&stop_tests]() noexcept { return ++stop_tests == 3; });
    const int kept = end_marker<Message>(std::move(sent.kept));
    SlowBox<Message> wire = std::move(sent.next).close();
    const int tries = wire.tries;
    const int value = end_marker<Message>(wire.held.take_last());
    std::move(wire.held).expect_empty("the line above took the marker");
    if (kept != 13) return fail("a stop between two tries did not give the marker back with its value");
    if (value != -1) return fail("a stop between two tries left a marker in the box");
    if (tries != 2) return fail("a stop before the third try did not come after two tries");
    return 0;
}

// A read of the region through a handle with the body Body.  A read calls its
// body, and a call is no consume, so the read refuses a body that holds a
// linear value by value.
template <class Handle, class Body>
concept CanReadRegionWith =
    requires(Handle handle, Body body) { std::move(handle).template read<Region>(std::move(body)); };

// ── A loan and its release ───────────────────────────────────────────
//
// The lender lends the region and waits for the release.  The borrower
// receives the loan into its slots, reads the region through the handle,
// and sends the release, which carries the loan back.  The lender then holds
// the token again, and its End handle gives it back.
template <class Brand>
struct LoanWire {
    Mailbox<s::Borrowed<int, Region, Brand>> lent{};
    Mailbox<s::Released<int, Region, Brand>> released{};
    int region_value = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

[[nodiscard]] int lend_and_release() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Brand = ::foundation::brand::brand_of_t<decltype(root)>;
    using Lend = s::Borrowed<int, Region, Brand>;
    using Release = s::Released<int, Region, Brand>;
    using Lends = s::Send<Lend, s::Recv<Release, s::End>>;
    using Borrows = s::Recv<Lend, s::Send<Release, s::End>>;
    using Wire = LoanWire<Brand>;
    auto lender = s::mint_permissioned_session<Lends>(ctx, Wire{.region_value = 40}, std::move(root));
    auto lender_waits = std::move(lender).send(
        1, [](Wire& wire, Lend&& lend) noexcept { return PutMessage<Lend>{}(wire.lent, std::move(lend)); });
    static_assert(
        fp::perm_set_equal(^^typename decltype(lender_waits)::perm_set, ^^fp::PermSet<s::LentOut<Region, Brand>>),
        "the token of the lender is parked in the LentOut slot");

    auto borrower = s::mint_session<Borrows>(ctx, Wire{});
    carry(lender_waits.resource().lent, borrower.resource().lent);
    auto [ticket, borrowing] = std::move(borrower).recv([](Wire& wire) noexcept { return take_message(wire.lent); });
    static_assert(
        fp::perm_set_equal(^^typename decltype(borrowing)::perm_set, ^^fp::PermSet<s::BorrowedIn<Region, Brand>>));
    const int region_value = lender_waits.resource().region_value;
    using HoldsTokenBody = decltype([held = std::declval<fp::Permission<Region, Brand>>()](
                                        fp::ReadView<Region, Brand> const&) noexcept { static_cast<void>(held); });
    using HoldsNoTokenBody = decltype([](fp::ReadView<Region, Brand> const&) noexcept {});
    static_assert(!CanReadRegionWith<decltype(borrowing), HoldsTokenBody>, "a read of a handle refuses a linear body");
    static_assert(CanReadRegionWith<decltype(borrowing), HoldsNoTokenBody>);
    auto [seen, reading] = std::move(borrowing).template read<Region>(
        [region_value](fp::ReadView<Region, Brand> const&) noexcept { return region_value; });
    auto borrower_done = std::move(reading).send(seen + ticket, [](Wire& wire, Release&& release) noexcept {
        return PutMessage<Release>{}(wire.released, std::move(release));
    });
    static_assert(std::is_same_v<typename decltype(borrower_done)::perm_set, NoTags>,
                  "the release carries the loan back out of the slots of the borrower");

    carry(borrower_done.resource().released, lender_waits.resource().released);
    auto [answer, lender_done] =
        std::move(lender_waits).recv([](Wire& wire) noexcept { return take_message(wire.released); });
    static_assert(std::is_same_v<typename decltype(lender_done)::perm_set, HoldsRegion<Brand>>,
                  "the release unparks the token of the lender");
    Wire borrower_wire = std::move(borrower_done).close();
    std::move(borrower_wire.lent.held).expect_empty("the borrower takes the loan out of its mailbox");
    std::move(borrower_wire.released.held).expect_empty("the carry moves the release to the lender");
    auto [wire, token] = std::move(lender_done).close();
    ::foundation::linear::drop(std::move(token));
    std::move(wire.lent.held).expect_empty("the carry moves the loan to the borrower");
    std::move(wire.released.held).expect_empty("the lender takes the release out of its mailbox");
    if (answer != 41) return fail("the release did not carry the value that the borrower read");
    return 0;
}

// ── The slots take no byte ───────────────────────────────────────────
template <class PS, class Policy>
using SendsPing = s::Handle<s::Send<int, s::End>, PS, Mailbox<int>, void, Policy>;

static_assert(sizeof(SendsPing<HoldsRegion<LayoutBrand>, s::check::Off>) == sizeof(SendsPing<NoTags, s::check::Off>));
static_assert(sizeof(SendsPing<HoldsRegion<LayoutBrand>, s::check::Enforced>)
              == sizeof(SendsPing<NoTags, s::check::Enforced>));
static_assert(sizeof(SendsPing<fp::PermSet<s::LentOut<Region, LayoutBrand>>, s::check::Enforced>)
              == sizeof(SendsPing<NoTags, s::check::Enforced>));
static_assert(sizeof(SendsPing<fp::PermSet<s::BorrowedIn<Region, LayoutBrand>>, s::check::Off>)
              == sizeof(SendsPing<NoTags, s::check::Off>));

// ── Each slot form travels on a session ──────────────────────────────
//
// A marker, a token and the hand-off of an endpoint move to another thread
// and hold no borrow.  So a protocol of them passes the transfer gate of
// each mint (fixy/session/Admission.h).  A View refers to the elements of
// its sender, and the gate refuses it.
using MovesRegion = s::Transferable<int, Region, LayoutBrand>;
using LendsRegion = s::Borrowed<int, Region, LayoutBrand>;
using ReleasesRegion = s::Released<int, Region, LayoutBrand>;
using RegionToken = fp::Permission<Region, LayoutBrand>;
using HandedPing = s::DelegatedSession<s::Send<int, s::End>, Mailbox<int>, s::DefaultAbandonmentPolicy, NoTags>;

static_assert(s::PayloadTransfers<MovesRegion> && s::PayloadTransfers<LendsRegion>
              && s::PayloadTransfers<ReleasesRegion> && s::PayloadTransfers<RegionToken>
              && s::PayloadTransfers<HandedPing>);
static_assert(s::ProtocolPayloadsTransfer<
              s::Offer<s::Recv<MovesRegion, s::End>, s::Send<RegionToken, s::Loop<s::Send<HandedPing, s::Continue>>>>>);
static_assert(!s::PayloadTransfers<::fixy::View<int const>>);

}  // namespace

int main() {
    if (const int rc = move_in_a_marker<s::Transferable>(7); rc != 0) return rc;
    if (const int rc = move_in_a_marker<s::Returned>(9); rc != 0) return rc;
    if (const int rc = refused_marker_is_taken(); rc != 0) return rc;
    if (const int rc = refused_marker_is_taken_with_a_stop_test(); rc != 0) return rc;
    if (const int rc = refused_marker_comes_back_at_a_stop(); rc != 0) return rc;
    if (const int rc = lend_and_release(); rc != 0) return rc;
    ::fixy::check::pass(kTest, "test_session_slot_forms: OK\n");
    return 0;
}
