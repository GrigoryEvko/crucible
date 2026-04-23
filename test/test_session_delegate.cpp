// Runtime harness for the Delegate/Accept combinators (task #337).
// The bulk of coverage is in-header self-test static_asserts; this
// file exercises the runtime `delegate()` / `accept()` methods end
// to end through an in-memory transport, and integrates the static
// test into ctest.

#include <crucible/safety/SessionDelegate.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
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

// ── Protocols for the test ─────────────────────────────────────────

// The delegated session's protocol: simple request-response.
struct PingReq { int payload; };
struct PingAck { int echoed; };
using DelegatedProto = Send<PingReq, Recv<PingAck, End>>;

// The outer (carrier) session's protocol: receive a name string,
// delegate a DelegatedProto-typed session, end.
using CarrierProto = Recv<std::string,
                          Delegate<DelegatedProto, End>>;

// Peer (dual): send a name string, accept a DelegatedProto session, end.
using CarrierPeer  = dual_of_t<CarrierProto>;
// CarrierPeer is Send<std::string, Accept<DelegatedProto, End>>

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

// ── End-to-end exercise ────────────────────────────────────────────

int run_carrier() {
    std::deque<std::string> wire;

    // Carrier resources
    MockChannel carrier_alice{.name = "carrier-alice", .wire_bytes = &wire};
    MockChannel carrier_bob  {.name = "carrier-bob",   .wire_bytes = &wire};

    // Establish carrier channel
    auto [alice_h, bob_h] =
        establish_channel<CarrierProto>(std::move(carrier_alice),
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
        make_session_handle<DelegatedProto>(std::move(delegated_src));

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

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_carrier(); rc != 0) return rc;
    std::puts("session_delegate: runtime delegate/accept round-trip OK");
    return 0;
}
