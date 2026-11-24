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
[[nodiscard]] ::fixy::Option<Message> put_message(Mailbox<Message>& box, Message&& message) noexcept {
    if (box.held.size() != 0) return ::fixy::Option<Message>::some(std::move(message));
    static_cast<void>(box.held.append(std::move(message)).expect("the mailbox is empty"));
    return ::fixy::none;
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
// the token back at close(), with the brand of the root.
template <template <class, class, class> class Marker>
[[nodiscard]] int move_in_a_marker(int value) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Brand = ::foundation::brand::brand_of_t<decltype(root)>;
    using Message = Marker<int, Region, Brand>;
    auto sender = s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, Mailbox<Message>{}, std::move(root));
    static_assert(std::is_same_v<typename decltype(sender)::perm_set, HoldsRegion<Brand>>);
    auto sender_done = std::move(sender).send(value, put_message<Message>);
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
        1, [](Wire& wire, Lend&& lend) noexcept { return put_message(wire.lent, std::move(lend)); });
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
        return put_message(wire.released, std::move(release));
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
    if (const int rc = lend_and_release(); rc != 0) return rc;
    ::fixy::check::pass(kTest, "test_session_slot_forms: OK\n");
    return 0;
}
