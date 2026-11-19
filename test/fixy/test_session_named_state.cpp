// The named state of fixy/session/Protocol.h: Named<Name> stands for the
// member type `protocol` of the class Name.
//
// The structural operations see through a name, and the type-building ones
// keep it: the dual, the composition, the canonical spelling and the two
// erasures give a named state for a named state.  A name hides nothing from
// a gate: each refusal of a protocol is also a refusal of the protocol
// behind a name, and a checkpoint primitive behind a name takes effect.  The
// protocol of a name is closed, so a Continue in it cannot bind a Loop above
// the name.  A name that reaches itself is refused with
// Protocol_Named_State_Cycle, in the fixture neg_sess_named_state_cycle.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Checkpoint.h>
#include <fixy/session/Crash.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>
#include <fixy/session/Projection.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>

#include <meta>
#include <type_traits>
#include <utility>

namespace s = ::fixy::session;
namespace tr = ::fixy::session::transition;
namespace fp = ::foundation::permissions;
namespace fe = ::foundation::effects;

// A label key needs a stable identity, so the labels stand in a named
// namespace (rule 7 of the section on branches and labels of
// fixy/session/Fold.h).
namespace named_state_test {

struct Hello {};
struct Bye {};

}  // namespace named_state_test

namespace {

using ::fixy::check::pass;
using ::named_state_test::Bye;
using ::named_state_test::Hello;

struct Alice {};
struct Bob {};

struct Region {
    using permission_row = fe::Row<>;
};
using HoldsRegion = fp::PermSet<Region>;

// ── Names ────────────────────────────────────────────────────────────

struct Closing {
    using protocol = s::Send<int, s::End>;
};
struct Ends {
    using protocol = s::End;
};
struct Repeats {
    using protocol = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
};
struct Commits {
    using protocol = s::Select<s::Commit<s::End>, s::Roll>;
};
struct NoProtocol {};

// ── A name stands for its protocol ───────────────────────────────────

static_assert(s::is_named_state_v<s::Named<Closing>>);
static_assert(std::is_same_v<s::named_state_protocol_t<s::Named<Closing>>, Closing::protocol>);
static_assert(s::is_send_v<s::Named<Closing>> && s::is_well_formed_v<s::Named<Closing>>);
static_assert(s::is_terminal_state_v<s::Named<Ends>>);
static_assert(s::is_well_formed_v<s::Named<Repeats>>);

// The registry of the session layer is the namespace of its combinators.
inline constexpr std::meta::info kRegistry = ^^::fixy::session::combinators;

// The class of a name is no combinator, it holds a member type `protocol`,
// and the unit defines it before a walk meets it.  Each other argument is no
// node of the registry, and a gate refuses it with
// Protocol_Unregistered_Combinator, in the fixture
// neg_sess_named_state_hides_a_pin.
static_assert(!tr::is_registered(kRegistry, ^^s::Named<s::VendorPinned<s::VendorBackend::NV, s::Send<int, s::End>>>));
static_assert(!tr::is_registered(kRegistry, ^^s::Named<NoProtocol>));
static_assert(!tr::is_registered(kRegistry, ^^s::Named<int>));
static_assert(tr::is_registered(kRegistry, ^^s::Named<Closing>));

// ── The type-building operations keep the name ───────────────────────

// The dual of a name is a name with the dual protocol, and the dual of the
// dual is the first name.  A protocol that is its own dual keeps its name.
using ClosingDual = s::dual_of_t<s::Named<Closing>>;
static_assert(s::is_named_state_v<ClosingDual>);
static_assert(std::is_same_v<s::named_state_protocol_t<ClosingDual>, s::Recv<int, s::End>>);
static_assert(std::is_same_v<s::dual_of_t<ClosingDual>, s::Named<Closing>>);
static_assert(s::is_dual_v<s::Named<Closing>, ClosingDual>);
static_assert(std::is_same_v<s::dual_of_t<s::Named<Ends>>, s::Named<Ends>>);

// The composition and the erasures give a name.  A fold that keeps the
// protocol keeps the name.
using Composed = s::compose_t<s::Named<Closing>, s::Send<long, s::End>>;
static_assert(s::is_named_state_v<Composed>);
static_assert(std::is_same_v<s::named_state_protocol_t<Composed>, s::Send<int, s::Send<long, s::End>>>);
static_assert(std::is_same_v<s::canonical_t<s::Named<Closing>>, s::Named<Closing>>);
static_assert(std::is_same_v<s::erase_crash_t<s::Named<Closing>>, s::Named<Closing>>);
using Erased = s::checkpoint_erase_t<s::Named<Commits>>;
static_assert(s::is_named_state_v<Erased>);
static_assert(std::is_same_v<s::named_state_protocol_t<Erased>, s::Select<s::End, s::End>>);

// ── A name hides nothing from a gate ─────────────────────────────────

// A payload that no endpoint sends.
struct SendsCrash {
    using protocol = s::Send<s::Crash<Alice>, s::End>;
};
static_assert(!s::is_well_formed_v<s::Named<SendsCrash>>);

// A choice with no label branch.
struct Empty {
    using protocol = s::Select<>;
};
static_assert(s::is_empty_choice_v<s::Select<s::Send<int, s::End>, s::Named<Empty>>>);

// Two peers.
struct TwoPeers {
    using protocol = s::Send<s::PeerMsg<Alice, Hello, int>, s::Send<s::PeerMsg<Bob, Hello, int>, s::End>>;
};
static_assert(!s::NamesAtMostOnePeer<s::Named<TwoPeers>>);

// A loan that stays open at End, and a token that the set does not hold.
struct Lends {
    using protocol = s::Send<s::Borrowed<int, Region>, s::End>;
};
// A message that moves the token of a region.  A bare token of a brand is
// no slot form: the slots keep each token on the identity of its tag, so a
// step cannot give a token of a brand.
struct TokenBrand final {};
struct SendsToken {
    using protocol = s::Send<s::Transferable<int, Region>, s::End>;
};
struct SendsBrandedToken {
    using protocol = s::Send<fp::Permission<Region, TokenBrand>, s::End>;
};
static_assert(s::PermissionFlowCloses<s::Named<SendsToken>, HoldsRegion>);
static_assert(!s::PermissionFlowCloses<s::Named<SendsToken>, fp::EmptyPermSet>);
static_assert(!s::PermissionFlowCloses<s::Named<SendsBrandedToken>, HoldsRegion>, "the slots give no token of a brand");
static_assert(!s::PermissionFlowCloses<s::Select<s::End, s::Named<Lends>>, HoldsRegion>);

// The row of a name is the row of its protocol.
using Io = fe::Computation<fe::Row<fe::Effect::IO>, int>;
struct SendsIo {
    using protocol = s::Send<Io, s::End>;
};
static_assert(
    std::is_same_v<s::protocol_payload_row_t<s::Named<SendsIo>>, s::protocol_payload_row_t<s::Send<Io, s::End>>>);
static_assert(!std::is_same_v<s::protocol_payload_row_t<s::Named<SendsIo>>, fe::Row<>>);

// A crash branch behind a name is no crash branch of its Offer, so the
// crash session refuses the protocol.
struct CrashArm {
    using protocol = s::Recv<s::Crash<Alice>, s::End>;
};
static_assert(s::is_crash_well_formed_v<s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Alice>, s::End>>>);
static_assert(!s::is_crash_well_formed_v<s::Offer<s::Recv<int, s::End>, s::Named<CrashArm>>>);

// A keyed label step stands before a name, not behind one.
struct SaysHello {
    using protocol = s::Send<s::Labelled<Hello, int>, s::End>;
};
struct SaysBye {
    using protocol = s::End;
};
using KeyedBehindName = s::Select<s::Named<SaysHello>, s::Send<s::Labelled<Bye, int>, s::End>>;
using KeyedBeforeName =
    s::Select<s::Send<s::Labelled<Hello, int>, s::End>, s::Send<s::Labelled<Bye, int>, s::Named<SaysBye>>>;
static_assert(tr::first_faulty_choice(kRegistry, ^^KeyedBehindName).fault == tr::choice_fault::keyed_label_below_root);
static_assert(s::is_well_formed_v<KeyedBeforeName>);

// The protocol of a name is closed.  A Continue in it cannot bind a Loop
// above the name, and a Loop in it binds its own Continue.
struct LoopsBack {
    using protocol = s::Send<int, s::Continue>;
};
static_assert(!s::is_well_formed_v<s::Loop<s::Select<s::Named<LoopsBack>, s::End>>>);
static_assert(s::is_well_formed_v<s::Loop<s::Select<s::Send<int, s::Continue>, s::Named<Repeats>>>>);

// The refinement relation compares the protocol of a name, so a named state
// and its protocol refine each other, and a named state refines no other
// protocol.
static_assert(s::EquivalentSync<s::Named<Closing>, Closing::protocol>);
static_assert(!s::SubtypeSync<s::Named<Closing>, s::Recv<int, s::End>>);

// ── A checkpoint primitive behind a name ─────────────────────────────

// The decider commits or rolls back through a name.  The check of the pair
// and the door of the session read each branch under its names.
struct SavesAndSends {
    using protocol = s::Commit<s::Send<int, s::End>>;
};
struct RollsBack {
    using protocol = s::Roll;
};
using NamedDecider = s::Select<s::Named<SavesAndSends>, s::Named<RollsBack>>;
using NamedFollower = s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>;
static_assert(s::checkpoint_verdict_of(^^NamedDecider, ^^NamedFollower) == s::CheckpointVerdict::Compliant);

// A vendor pin and a name in the two orders.  The name of a pinned Roll and
// the pin of a named Roll each read as the Roll.
struct PinnedRollsBack {
    using protocol = s::VendorPinned<s::VendorBackend::NV, s::Roll>;
};
using NameOverPinDecider = s::Select<s::Named<SavesAndSends>, s::Named<PinnedRollsBack>>;
using PinOverNameDecider =
    s::Select<s::Named<SavesAndSends>, s::VendorPinned<s::VendorBackend::NV, s::Named<RollsBack>>>;
static_assert(!s::is_vendor_pinned_v<s::Named<PinnedRollsBack>>);
static_assert(s::checkpoint_verdict_of(^^NameOverPinDecider, ^^NamedFollower) == s::CheckpointVerdict::Compliant);
static_assert(s::checkpoint_verdict_of(^^PinOverNameDecider, ^^NamedFollower) == s::CheckpointVerdict::Compliant);

// A cycle of names with a step between the names: the check of the shape
// stops at its depth bound, so the verdict ends.
struct SendsForever {
    using protocol = s::Send<int, s::Named<SendsForever>>;
};
struct ReceivesForever {
    using protocol = s::Recv<int, s::Named<ReceivesForever>>;
};
static_assert(s::checkpoint_verdict_of(^^s::Named<SendsForever>, ^^s::Named<ReceivesForever>)
              == s::CheckpointVerdict::NotCheckpointShaped);

// ── A session over names ─────────────────────────────────────────────

// A carrier with no peer, so a choice of the checkpoint session puts no
// label on a wire.
struct LocalLog {
    static constexpr s::Network session_network = s::Network::Local;
    int words = 0;
};

struct LeftWire {
    int words = 0;
};
struct RightWire {
    int words = 0;
};

// A name of a Loop resolves before the handle enters the Loop, so the first
// handle stands at the choice of the body, in the loop context of the Loop.
void channel_over_a_named_loop(::fixy::TestRunnerCtx const& test) {
    auto [left, right] = s::mint_test_channel<s::Named<Repeats>>(test, LeftWire{}, RightWire{});
    static_assert(s::is_select_v<typename decltype(left)::protocol>);
    auto left_end = std::move(left).select_local<1>(test);
    auto right_end = std::move(right).pick_local<1>(test);
    static_cast<void>(std::move(left_end).close());
    static_cast<void>(std::move(right_end).close());
    pass(test, "  channel_over_a_named_loop: PASSED\n");
}

// A session whose protocol reaches one name from two branches.
void session_over_a_shared_name(::fixy::TestRunnerCtx const& test) {
    using Shared = s::Select<s::Named<Ends>, s::Named<Ends>>;
    auto [left, right] = s::mint_test_channel<Shared>(test, LeftWire{}, RightWire{});
    auto left_end = std::move(left).select_local<0>(test);
    auto right_end = std::move(right).pick_local<0>(test);
    static_cast<void>(std::move(left_end).close());
    static_cast<void>(std::move(right_end).close());
    pass(test, "  session_over_a_shared_name: PASSED\n");
}

// The named Roll returns the session to its checkpoint, the start, and the
// named Commit takes a checkpoint at the continuation of the Commit.  A door
// that read the named branch as a plain branch would stand at Roll and at the
// state of the name.
void checkpoint_primitives_behind_names(::fixy::TestRunnerCtx const& test) {
    auto start = s::mint_checkpoint_session<NamedDecider, NamedFollower>(test, LocalLog{});
    auto rolled = std::move(start).select<1>(s::no_label);
    static_assert(std::is_same_v<typename decltype(rolled)::protocol, NamedDecider>);
    auto saved = std::move(rolled).select<0>(s::no_label);
    static_assert(std::is_same_v<typename decltype(saved)::protocol, s::Send<int, s::End>>);
    static_assert(std::is_same_v<typename decltype(saved)::frame::saved, s::Send<int, s::End>>);
    std::move(saved).detach(s::detach_reason::TestInstrumentation{});
    pass(test, "  checkpoint_primitives_behind_names: PASSED\n");
}

// A Roll behind a name and a pin, in each order, returns the session to the
// start, and the named Commit then takes its checkpoint.
template <typename Decider>
void rolls_back_through_a_pin_and_a_name(::fixy::TestRunnerCtx const& test) {
    auto start = s::mint_checkpoint_session<Decider, NamedFollower>(test, LocalLog{});
    auto rolled = std::move(start).template select<1>(s::no_label);
    static_assert(std::is_same_v<typename decltype(rolled)::protocol, Decider>);
    auto saved = std::move(rolled).template select<0>(s::no_label);
    static_assert(std::is_same_v<typename decltype(saved)::protocol, s::Send<int, s::End>>);
    std::move(saved).detach(s::detach_reason::TestInstrumentation{});
}

void checkpoint_through_a_pin_and_a_name(::fixy::TestRunnerCtx const& test) {
    rolls_back_through_a_pin_and_a_name<NameOverPinDecider>(test);
    rolls_back_through_a_pin_and_a_name<PinOverNameDecider>(test);
    pass(test, "  checkpoint_through_a_pin_and_a_name: PASSED\n");
}

// The handle steps a name of a pinned choice, and a pin of a named loop, to
// the choice under the two wrappers.
struct PinnedChoice {
    using protocol = s::VendorPinned<s::VendorBackend::NV, s::Select<s::End, s::End>>;
};

void channel_through_a_pin_and_a_name(::fixy::TestRunnerCtx const& test) {
    auto [left, right] = s::mint_test_channel<s::Named<PinnedChoice>>(test, LeftWire{}, RightWire{});
    static_assert(s::is_select_v<typename decltype(left)::protocol>);
    auto left_end = std::move(left).select_local<0>(test);
    auto right_end = std::move(right).pick_local<0>(test);
    static_cast<void>(std::move(left_end).close());
    static_cast<void>(std::move(right_end).close());

    auto [pinned_left, pinned_right] =
        s::mint_test_channel<s::VendorPinned<s::VendorBackend::NV, s::Named<Repeats>>>(test, LeftWire{}, RightWire{});
    static_assert(s::is_select_v<typename decltype(pinned_left)::protocol>);
    auto pinned_left_end = std::move(pinned_left).select_local<1>(test);
    auto pinned_right_end = std::move(pinned_right).pick_local<1>(test);
    static_cast<void>(std::move(pinned_left_end).close());
    static_cast<void>(std::move(pinned_right_end).close());
    pass(test, "  channel_through_a_pin_and_a_name: PASSED\n");
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    channel_over_a_named_loop(test);
    session_over_a_shared_name(test);
    checkpoint_primitives_behind_names(test);
    checkpoint_through_a_pin_and_a_name(test);
    channel_through_a_pin_and_a_name(test);
    return 0;
}
