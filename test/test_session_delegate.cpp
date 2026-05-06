// Runtime harness for the Delegate/Accept combinators (task #337).
// The bulk of coverage is in-header self-test static_asserts; this
// file exercises the runtime `delegate()` / `accept()` methods end
// to end through an in-memory transport, and integrates the static
// test into ctest.

#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionMint.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety::proto;

// ── In-memory resource (the "channel") ─────────────────────────────

// A mock transport resource.  The main carrier session's resource is
// one instance; the delegated session's resource is another instance.
// The transport functions simply copy bytes between instances via a
// shared side-channel queue.

struct MockChannel {
    std::string name                        {};     // diagnostic label
    std::deque<std::string>* wire_bytes    = nullptr;  // shared with peer
    std::optional<std::string> pending_delegation = std::nullopt;
};

struct IntChannel {
    int last_int = 0;
};

// ── Protocols for the test ─────────────────────────────────────────

// The delegated session's protocol: simple request-response.
struct PingReq { int payload; };
struct PingAck { int echoed; };
struct HotShardChunk { int shard_id; };
struct ReshardAck { int epoch; int generation; };
struct DelegatedRecipient {};
using DelegatedProto = Send<PingReq, Recv<PingAck, End>>;
using DelegatedOutboundProto = Send<PingReq, End>;
using HotTierHandle = Send<HotShardChunk, End>;
using RecvAck = Recv<ReshardAck, End>;

using CarrierCrashRecovery = Offer<
    Recv<PingAck, End>,
    Recv<Crash<DelegatedRecipient>, End>>;

using CarrierSurvivesRecipientCrash =
    Delegate<DelegatedOutboundProto, CarrierCrashRecovery>;

// The outer (carrier) session's protocol: receive a name string,
// delegate a DelegatedProto-typed session, end.
using CarrierProto = Recv<std::string,
                          Delegate<DelegatedProto, End>>;

// Peer (dual): send a name string, accept a DelegatedProto session, end.
using CarrierPeer  = dual_of_t<CarrierProto>;
// CarrierPeer is Send<std::string, Accept<DelegatedProto, End>>

using EpochedReshardDelegate =
    EpochedDelegate<HotTierHandle, RecvAck, 5, 3>;
using EpochedReshardAccept = dual_of_t<EpochedReshardDelegate>;

using EpochedReshardDelegatePsh =
    EpochedDelegate<DelegatedSession<HotTierHandle, EmptyPermSet>,
                    RecvAck,
                    6,
                    3>;
using EpochedReshardAcceptPsh = dual_of_t<EpochedReshardDelegatePsh>;

// ── Transports ─────────────────────────────────────────────────────

static int delegation_transfers_observed = 0;

void send_string(MockChannel& res, std::string&& value) {
    res.wire_bytes->push_back(std::move(value));
}

std::string recv_string(MockChannel& res) {
    std::string out = std::move(res.wire_bytes->front());
    res.wire_bytes->pop_front();
    return out;
}

// Delegation transport: the carrier's sender hands off a
// DelegatedResource.  In the mock, we stash the delegated resource's
// name for the peer to pick up.
void transport_delegate(MockChannel& carrier_res,
                         MockChannel&& delegated_res) {
    carrier_res.wire_bytes->push_back("DELEGATED:" + delegated_res.name);
    ++delegation_transfers_observed;
}

MockChannel transport_accept(MockChannel& carrier_res) {
    std::string token = std::move(carrier_res.wire_bytes->front());
    carrier_res.wire_bytes->pop_front();
    // Strip the "DELEGATED:" prefix; the rest is the delegated
    // endpoint's name.
    std::string delegated_name = token.substr(std::strlen("DELEGATED:"));
    return MockChannel{.name        = std::move(delegated_name),
                        .wire_bytes  = carrier_res.wire_bytes};
}

std::size_t recv_branch_index(MockChannel& res) {
    std::string token = std::move(res.wire_bytes->front());
    res.wire_bytes->pop_front();
    return static_cast<std::size_t>(std::atoi(token.c_str()));
}

Crash<DelegatedRecipient> recv_delegated_crash(MockChannel&) noexcept {
    return Crash<DelegatedRecipient>{};
}

void send_int(IntChannel& res, int value) noexcept {
    res.last_int = value;
}

void send_hot_shard_chunk(MockChannel& res, HotShardChunk&& chunk) noexcept {
    res.wire_bytes->push_back("HOT:" + std::to_string(chunk.shard_id));
}

void send_reshard_ack(MockChannel& res, ReshardAck&& ack) noexcept {
    res.wire_bytes->push_back(
        "RESHARD_ACK:" + std::to_string(ack.epoch) + ":" +
        std::to_string(ack.generation));
}

ReshardAck recv_reshard_ack(MockChannel& res) {
    std::string token = std::move(res.wire_bytes->front());
    res.wire_bytes->pop_front();
    constexpr std::string_view prefix = "RESHARD_ACK:";
    const auto second_sep = token.find(':', prefix.size());
    return ReshardAck{
        .epoch = std::atoi(token.c_str() + prefix.size()),
        .generation = std::atoi(token.c_str() + second_sep + 1)
    };
}

// ── End-to-end exercise ────────────────────────────────────────────

int run_carrier() {
    std::deque<std::string> wire;

    // Carrier resources
    MockChannel carrier_alice{.name = "carrier-alice", .wire_bytes = &wire};
    MockChannel carrier_bob  {.name = "carrier-bob",   .wire_bytes = &wire};

    // Establish carrier channel
    auto [alice_h, bob_h] =
        mint_channel<CarrierProto>(std::move(carrier_alice),
                                         std::move(carrier_bob));

    // Bob initiates by sending the delegated-endpoint's name as a
    // label string.  (In a real transport the label would be a
    // capability ID; here it's just a string.)
    auto bob_h2 = std::move(bob_h).send(std::string{"delegated-ping"},
                                         send_string);

    // Alice reads the label
    auto [label, alice_h2] = std::move(alice_h).recv(recv_string);
    if (label != "delegated-ping") {
        std::fprintf(stderr, "label mismatch: %s\n", label.c_str());
        return 1;
    }

    // NOW: Alice has a CarrierProto (Delegate<…>) handle.  She needs
    // to produce a DelegatedProto-handle to hand off.  Construct a
    // fresh delegated-session resource (in a real transport, this
    // might be a channel Alice had open from earlier work).
    MockChannel delegated_src{.name = "src-channel-for-ping",
                               .wire_bytes = &wire};
    auto delegated_handle =
        mint_session_handle<DelegatedProto>(std::move(delegated_src));

    // Alice delegates: alice_h2 is Delegate<DelegatedProto, End>.
    // After delegate(), alice_h3 is End.
    auto alice_h3 = std::move(alice_h2).delegate(
        std::move(delegated_handle),
        transport_delegate);

    // Bob's side: accept the delegated endpoint.
    auto [bob_delegated, bob_h3] =
        std::move(bob_h2).accept(transport_accept);

    // Verify the delegation transport fired exactly once.
    if (delegation_transfers_observed != 1) {
        std::fprintf(stderr, "expected exactly 1 delegation transfer, saw %d\n",
                     delegation_transfers_observed);
        return 1;
    }

    // Bob now OWNS a SessionHandle<DelegatedProto, MockChannel> — he
    // may use it.  Terminate the carrier channel first.
    auto alice_res_back = std::move(alice_h3).close();
    auto bob_res_back   = std::move(bob_h3).close();
    (void)alice_res_back;
    (void)bob_res_back;

    // Bob's delegated handle's underlying resource should carry the
    // name "src-channel-for-ping" that Alice transferred.
    const auto& bob_delegated_resource = bob_delegated.resource();
    if (bob_delegated_resource.name != "src-channel-for-ping") {
        std::fprintf(stderr,
                     "delegated handle's resource name mismatch: got '%s'\n",
                     bob_delegated_resource.name.c_str());
        return 1;
    }

    // PROOF OF FIRST-CLASS-NESS: Bob actually USES the delegated
    // handle — it's a real SessionHandle<DelegatedProto, MockChannel>
    // that he can step through.  DelegatedProto is
    // Send<PingReq, Recv<PingAck, End>>, so Bob's handle is at Send.
    //
    // To actually drive send + recv through the delegated channel we
    // need a matching peer for the delegated endpoint.  Construct one
    // on the fly (in production, the peer lives somewhere else — the
    // whole point of delegation is handoff to a pre-existing peer).
    std::deque<std::string> delegated_wire;
    MockChannel delegated_peer_res{.name = "delegated-peer",
                                    .wire_bytes = &delegated_wire};
    // Rebind bob_delegated's wire to match — simulating that the peer
    // at the other end of the delegated channel is connected.
    bob_delegated.resource().wire_bytes = &delegated_wire;
    auto delegated_peer_h =
        mint_session_handle<dual_of_t<DelegatedProto>>(
            std::move(delegated_peer_res));

    // Bob sends a PingReq through his delegated handle.  (Transport
    // lambdas marked noexcept: test harness accepts std::terminate
    // on OOM / alloc failure — not a real failure mode for this
    // bounded in-memory test.)
    auto send_req = [](MockChannel& res, PingReq&& req) noexcept {
        res.wire_bytes->push_back("REQ:" + std::to_string(req.payload));
    };
    auto recv_req = [](MockChannel& res) noexcept -> PingReq {
        std::string s = std::move(res.wire_bytes->front());
        res.wire_bytes->pop_front();
        return PingReq{.payload = std::atoi(s.data() + std::strlen("REQ:"))};
    };
    auto send_ack = [](MockChannel& res, PingAck&& ack) noexcept {
        res.wire_bytes->push_back("ACK:" + std::to_string(ack.echoed));
    };
    auto recv_ack = [](MockChannel& res) noexcept -> PingAck {
        std::string s = std::move(res.wire_bytes->front());
        res.wire_bytes->pop_front();
        return PingAck{.echoed = std::atoi(s.data() + std::strlen("ACK:"))};
    };

    auto bob_delegated_2 = std::move(bob_delegated).send(
        PingReq{.payload = 42}, send_req);
    auto [req, delegated_peer_h2] = std::move(delegated_peer_h).recv(recv_req);
    if (req.payload != 42) {
        std::fprintf(stderr, "delegated req payload mismatch: got %d\n",
                     req.payload);
        return 1;
    }
    auto delegated_peer_h3 = std::move(delegated_peer_h2).send(
        PingAck{.echoed = req.payload}, send_ack);
    auto [ack, bob_delegated_3] = std::move(bob_delegated_2).recv(recv_ack);
    if (ack.echoed != 42) {
        std::fprintf(stderr, "delegated ack echo mismatch: got %d\n",
                     ack.echoed);
        return 1;
    }

    // Clean close on both delegated ends.
    (void)std::move(bob_delegated_3).close();
    (void)std::move(delegated_peer_h3).close();

    return 0;
}

int run_carrier_survives_delegated_recipient_crash() {
    std::deque<std::string> wire;
    wire.push_back("1");  // branch 1 is Recv<Crash<DelegatedRecipient>, End>

    auto carrier = mint_session_handle<CarrierCrashRecovery>(
        MockChannel{.name = "carrier-recovery", .wire_bytes = &wire});

    bool crash_branch_seen = false;
    int rc = std::move(carrier).branch(
        recv_branch_index,
        [&](auto branch_handle) -> int {
            using BranchHandle = decltype(branch_handle);
            using BranchProto  = typename BranchHandle::protocol;

            if constexpr (std::is_same_v<
                              BranchProto,
                              Recv<Crash<DelegatedRecipient>, End>>) {
                auto [crash, end_handle] =
                    std::move(branch_handle).recv(recv_delegated_crash);
                (void)crash;
                crash_branch_seen = true;
                (void)std::move(end_handle).close();
                return 0;
            } else {
                std::fprintf(stderr,
                             "delegate crash recovery: wrong branch selected\n");
                return 1;
            }
        });

    if (rc != 0) return rc;
    if (!crash_branch_seen) {
        std::fprintf(stderr,
                     "delegate crash recovery: crash branch did not fire\n");
        return 1;
    }
    return 0;
}

int run_delegate_stop_composition() {
    using RecoveryProto = Recv<PingAck, End>;
    using DelegateStopNoOp =
        compose_t<Delegate<Stop, RecoveryProto>, Send<int, End>>;
    using QThenDelegateStop =
        compose_t<Send<int, End>, Delegate<Stop, RecoveryProto>>;

    static_assert(std::is_same_v<DelegateStopNoOp, Stop>);
    static_assert(std::is_same_v<QThenDelegateStop, Send<int, Stop>>);

    auto stop_handle = mint_session_handle<DelegateStopNoOp>(IntChannel{});
    (void)std::move(stop_handle).close();

    auto q_handle = mint_session_handle<QThenDelegateStop>(IntChannel{});
    auto stop_after_q = std::move(q_handle).send(7, send_int);
    IntChannel released = std::move(stop_after_q).close();
    if (released.last_int != 7) {
        std::fprintf(stderr,
                     "delegate stop composition: Q did not run first\n");
        return 1;
    }

    return 0;
}

int run_epoched_delegate_reshard() {
    std::deque<std::string> wire;

    SessionHandle<EpochedReshardDelegate, MockChannel> sender{
        MockChannel{.name = "reshard-sender", .wire_bytes = &wire}};

    using FreshRecipientCtx = EpochCtx<5, 3>;
    SessionHandle<EpochedReshardAccept, MockChannel, FreshRecipientCtx> recipient{
        MockChannel{.name = "reshard-recipient", .wire_bytes = &wire}};

    auto hot_handle = mint_session_handle<HotTierHandle>(
        MockChannel{.name = "hot-tier-shard", .wire_bytes = &wire});

    auto sender_waiting_for_ack = std::move(sender).delegate(
        std::move(hot_handle),
        transport_delegate);

    auto [accepted_hot_handle, recipient_can_ack] =
        std::move(recipient).accept(transport_accept);

    std::deque<std::string> hot_wire;
    accepted_hot_handle.resource().wire_bytes = &hot_wire;
    auto accepted_hot_done = std::move(accepted_hot_handle).send(
        HotShardChunk{.shard_id = 17},
        send_hot_shard_chunk);
    (void)std::move(accepted_hot_done).close();

    auto recipient_done = std::move(recipient_can_ack).send(
        ReshardAck{.epoch = 5, .generation = 3},
        send_reshard_ack);
    (void)std::move(recipient_done).close();

    auto [ack, sender_done] =
        std::move(sender_waiting_for_ack).recv(recv_reshard_ack);
    if (ack.epoch != 5 || ack.generation != 3) {
        std::fprintf(stderr,
                     "epoched reshard ack mismatch: got epoch=%d gen=%d\n",
                     ack.epoch,
                     ack.generation);
        return 1;
    }
    (void)std::move(sender_done).close();

    const std::string expected_hot_chunk = "HOT:17";
    if (hot_wire.empty() || hot_wire.front() != expected_hot_chunk) {
        std::fprintf(stderr, "hot shard handoff did not run delegated protocol\n");
        return 1;
    }
    hot_wire.pop_front();

    return 0;
}

int run_epoched_delegate_mint_reshard() {
    using Epoch6Ctx =
        EpochExecCtx<6, 3, ::crucible::effects::HotFgCtx>;

    struct RelayState {
        const char* name;
        int         epoch;
        int         generation;
        bool        alive;
    };

    const RelayState fleet[] = {
        {"relay-a", 5, 3, true},
        {"relay-b", 5, 3, true},
        {"relay-c", 5, 3, false},
        {"relay-d", 5, 3, true},
    };
    int live_relays = 0;
    for (RelayState const& relay : fleet) {
        if (relay.epoch != 5 || relay.generation != 3) {
            std::fprintf(stderr,
                         "unexpected pre-reshard coordinate for %s\n",
                         relay.name);
            return 1;
        }
        if (relay.alive) {
            ++live_relays;
        }
    }
    if (live_relays != 3) {
        std::fprintf(stderr, "expected three surviving relays after failure\n");
        return 1;
    }

    Epoch6Ctx epoch6{};
    std::deque<std::string> wire;

    auto sender = mint_session<EpochedReshardDelegatePsh>(
        epoch6,
        MockChannel{.name = "epoch6-reshard-sender", .wire_bytes = &wire});
    auto recipient = mint_session<EpochedReshardAcceptPsh>(
        epoch6,
        MockChannel{.name = "epoch6-reshard-recipient", .wire_bytes = &wire});
    auto hot_handle = mint_session<HotTierHandle>(
        epoch6,
        MockChannel{.name = "epoch6-hot-tier-shard", .wire_bytes = &wire});

    auto sender_waiting_for_ack = std::move(sender).delegate(
        std::move(hot_handle),
        transport_delegate);
    auto [accepted_hot_handle, recipient_can_ack] =
        std::move(recipient).accept(transport_accept);

    std::deque<std::string> hot_wire;
    accepted_hot_handle.resource().wire_bytes = &hot_wire;
    auto accepted_hot_done = std::move(accepted_hot_handle).send(
        HotShardChunk{.shard_id = 23},
        send_hot_shard_chunk);
    (void)std::move(accepted_hot_done).close();

    auto recipient_done = std::move(recipient_can_ack).send(
        ReshardAck{.epoch = 6, .generation = 3},
        send_reshard_ack);
    (void)std::move(recipient_done).close();

    auto [ack, sender_done] =
        std::move(sender_waiting_for_ack).recv(recv_reshard_ack);
    if (ack.epoch != 6 || ack.generation != 3) {
        std::fprintf(stderr,
                     "minted epoched reshard ack mismatch: got epoch=%d gen=%d\n",
                     ack.epoch,
                     ack.generation);
        return 1;
    }
    (void)std::move(sender_done).close();

    const std::string expected_hot_chunk = "HOT:23";
    if (hot_wire.empty() || hot_wire.front() != expected_hot_chunk) {
        std::fprintf(stderr,
                     "minted hot shard handoff did not run delegated protocol\n");
        return 1;
    }
    hot_wire.pop_front();

    return 0;
}

// ── Compile-time: DelegatesTo concept + assert_delegates_to helper ─
static_assert(DelegatesTo<Delegate<DelegatedProto, End>, DelegatedProto>);
static_assert(AcceptsFrom<Accept<DelegatedProto, End>,   DelegatedProto>);
static_assert(DelegatesTo<EpochedReshardDelegate, HotTierHandle>);
static_assert(AcceptsFrom<EpochedReshardAccept, HotTierHandle>);
static_assert(std::is_same_v<
    EpochedReshardAccept,
    EpochedAccept<HotTierHandle, Send<ReshardAck, End>, 5, 3>>);
static_assert(std::is_same_v<
    EpochedReshardAcceptPsh,
    EpochedAccept<DelegatedSession<HotTierHandle, EmptyPermSet>,
                  Send<ReshardAck, End>,
                  6,
                  3>>);
static_assert(PermissionedSessionHandle<
    EpochedReshardDelegatePsh,
    EmptyPermSet,
    MockChannel,
    EpochCtx<6, 3>>::protocol_name().contains("EpochedDelegate"));
static_assert(PermissionedSessionHandle<
    EpochedReshardAcceptPsh,
    EmptyPermSet,
    MockChannel,
    EpochCtx<6, 3>>::protocol_name().contains("EpochedAccept"));
static_assert(session_loop_ctx_epoch_satisfies_v<EpochCtx<5, 3>, 5, 3>);
static_assert(session_loop_ctx_epoch_satisfies_v<EpochCtx<6, 3>, 5, 3>);
static_assert(!session_loop_ctx_epoch_satisfies_v<EpochCtx<4, 3>, 5, 3>);
static_assert(!session_loop_ctx_epoch_satisfies_v<EpochCtx<5, 2>, 5, 3>);
static_assert(session_loop_ctx_epoch_matches_v<EpochCtx<6, 3>, 6, 3>);
static_assert(!session_loop_ctx_epoch_matches_v<EpochCtx<6, 3>, 6, 2>);
using OldRelayCtx = EpochExecCtx<5, 3, ::crucible::effects::HotFgCtx>;
using NewRelayCtx = EpochExecCtx<6, 3, ::crucible::effects::HotFgCtx>;
using NewerRelayCtx = EpochExecCtx<7, 3, ::crucible::effects::HotFgCtx>;
using RelayAOldCtx = EpochExecCtx<5, 3, ::crucible::effects::HotFgCtx>;
using RelayBOldCtx = EpochExecCtx<5, 3, ::crucible::effects::HotFgCtx>;
using RelayDOldCtx = EpochExecCtx<5, 3, ::crucible::effects::HotFgCtx>;
using RelayANewCtx = EpochExecCtx<6, 3, ::crucible::effects::HotFgCtx>;
using RelayBNewCtx = EpochExecCtx<6, 3, ::crucible::effects::HotFgCtx>;
using RelayDNewCtx = EpochExecCtx<6, 3, ::crucible::effects::HotFgCtx>;
using WeakenedGenerationPsh =
    EpochedDelegate<DelegatedSession<HotTierHandle, EmptyPermSet>,
                    RecvAck,
                    6,
                    2>;
static_assert(CtxFitsPermissionedProtocol<
    EpochedReshardDelegatePsh, NewRelayCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    EpochedReshardDelegatePsh, OldRelayCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    WeakenedGenerationPsh, NewRelayCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    EpochedReshardDelegatePsh, RelayAOldCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    EpochedReshardDelegatePsh, RelayBOldCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    EpochedReshardDelegatePsh, RelayDOldCtx, EmptyPermSet>);
static_assert(CtxFitsPermissionedProtocol<
    EpochedReshardDelegatePsh, RelayANewCtx, EmptyPermSet>);
static_assert(CtxFitsPermissionedProtocol<
    EpochedReshardAcceptPsh, NewRelayCtx, EmptyPermSet>);
static_assert(CtxFitsPermissionedProtocol<
    EpochedReshardAcceptPsh, NewerRelayCtx, EmptyPermSet>);
static_assert(!CtxFitsPermissionedProtocol<
    EpochedReshardAcceptPsh, OldRelayCtx, EmptyPermSet>);
static_assert(CtxFitsPermissionedProtocol<
    EpochedReshardAcceptPsh, RelayBNewCtx, EmptyPermSet>);
static_assert(CtxFitsPermissionedProtocol<
    EpochedReshardAcceptPsh, RelayDNewCtx, EmptyPermSet>);
static_assert(is_well_formed<
    EpochedReshardAccept,
    EpochCtx<5, 3>>::value);
static_assert(!is_well_formed<
    EpochedReshardAccept,
    EpochCtx<4, 3>>::value);
static_assert(std::is_same_v<
    typename SessionHandle<EpochedReshardAccept,
                           MockChannel,
                           EpochCtx<5, 3>>::protocol,
    EpochedReshardAccept>);

static_assert(std::is_same_v<
    delegated_crash_propagation_t<
        DelegatedOutboundProto,
        DelegatedRecipient,
        CarrierCrashRecovery>,
    Recovers<End>>);
static_assert(every_offer_has_crash_branch_for_peer_v<
    CarrierSurvivesRecipientCrash,
    DelegatedRecipient>);

// Invoking the assert helpers at namespace scope would trigger their
// consteval path even if we don't have a top-level consteval context;
// wrap in a constexpr fn for the static_assert.
consteval bool exercise_assert_helpers() {
    assert_delegates_to<Delegate<DelegatedProto, Send<int, End>>,
                         DelegatedProto>();
    assert_accepts_from<Accept<DelegatedProto, Recv<int, End>>,
                         DelegatedProto>();
    assert_delegated_crash_propagates<
        DelegatedOutboundProto,
        DelegatedRecipient,
        CarrierCrashRecovery>();
    return true;
}
static_assert(exercise_assert_helpers());

}  // anonymous namespace

int main() {
    if (int rc = run_carrier(); rc != 0) return rc;
    if (int rc = run_carrier_survives_delegated_recipient_crash(); rc != 0) {
        return rc;
    }
    if (int rc = run_delegate_stop_composition(); rc != 0) return rc;
    if (int rc = run_epoched_delegate_reshard(); rc != 0) return rc;
    if (int rc = run_epoched_delegate_mint_reshard(); rc != 0) return rc;
    std::puts("session_delegate: delegation + delegated-endpoint-usage OK");
    return 0;
}
