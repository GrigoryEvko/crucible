// Runtime harness for L5 association Δ ⊑_s G (task #345, SEPLOG-I3).
// Most coverage is in-header static_asserts; this file exercises
// association end-to-end: define a global type, derive the projected
// context, construct a refined context, verify both associate, run
// the projected 2PC protocol on handles whose types were LOOKED UP
// FROM THE CONTEXT via lookup_context_t.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionAssoc.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionGlobal.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

// ── Fixture tags ────────────────────────────────────────────────

struct TraceSession {};
struct Producer {};
struct Consumer {};

struct Event { int sequence; };

// ── Global type for a single-session trace ring ────────────────

using G_trace = Rec_G<Transmission<Producer, Consumer, Event, Var_G>>;

// Projection.
using ProducerProto = project_t<G_trace, Producer>;
using ConsumerProto = project_t<G_trace, Consumer>;

// Canonical Γ: projected directly from G.
using ReflexiveGamma = projected_context_t<G_trace, TraceSession>;

// Compile-time: Γ associates with G.
static_assert(is_associated_v<ReflexiveGamma, G_trace, TraceSession>);
static_assert(AssociatedWith<ReflexiveGamma, G_trace, TraceSession>);

// Γ's contents match the projections.
static_assert(std::is_same_v<
    lookup_context_t<ReflexiveGamma, TraceSession, Producer>,
    ProducerProto>);
static_assert(std::is_same_v<
    lookup_context_t<ReflexiveGamma, TraceSession, Consumer>,
    ConsumerProto>);

// ── Wire + transport lambdas ────────────────────────────────────

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

auto send_event = [](Wire& w, Event&& e) noexcept {
    w.bytes->push_back("EV:" + std::to_string(e.sequence));
};
auto recv_event = [](Wire& w) noexcept -> Event {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Event{std::atoi(s.data() + 3)};
};

// ── End-to-end: drive a handle whose type was LOOKED UP from Γ ──

int run_trace_via_context_lookup() {
    std::deque<std::string> wire;
    Wire p_res{&wire};
    Wire c_res{&wire};

    // Derive the handle protocols BY LOOKING UP in Γ — not by hand-
    // typing them.  This is the top-down methodology's payoff: one
    // G, one Γ, every role's handle type falls out of lookup.
    using ProducerLocal = lookup_context_t<ReflexiveGamma, TraceSession, Producer>;
    using ConsumerLocal = lookup_context_t<ReflexiveGamma, TraceSession, Consumer>;

    // Demonstrate the binary-duality invariant: for a two-role Γ
    // associated with a binary G, looking up one role gives the
    // dual of looking up the other.
    static_assert(std::is_same_v<dual_of_t<ProducerLocal>, ConsumerLocal>);

    auto [prod, cons] =
        establish_channel<ProducerLocal>(std::move(p_res), std::move(c_res));

    // Producer sends three Events through the looping Send<Event,
    // Continue>; Consumer receives each in order.
    auto p1 = std::move(prod).send(Event{0}, send_event);
    auto p2 = std::move(p1).send(Event{1}, send_event);
    auto p3 = std::move(p2).send(Event{2}, send_event);

    auto [e0, c1] = std::move(cons).recv(recv_event);
    auto [e1, c2] = std::move(c1).recv(recv_event);
    auto [e2, c3] = std::move(c2).recv(recv_event);

    if (e0.sequence != 0 || e1.sequence != 1 || e2.sequence != 2) {
        std::fprintf(stderr,
                     "assoc: event ordering broken (%d, %d, %d)\n",
                     e0.sequence, e1.sequence, e2.sequence);
        return 1;
    }

    // Both handles are in Loop<...Continue> state — inherently
    // infinite, no close branch.  Detach intentionally (SessionHandle
    // abandonment check otherwise fires in debug).
    std::move(p3).detach(detach_reason::InfiniteLoopProtocol{});
    std::move(c3).detach(detach_reason::InfiniteLoopProtocol{});
    return 0;
}

// ── 2PC: worked example of refined association ────────────────
//
// Coord's implementation commits unconditionally (one Select branch
// instead of two).  The refined Γ still associates with G_2PC.

struct My2PC {};
struct Coord    {};
struct Follower {};
struct Prepare {};
struct Vote    {};
struct Commit  {};
struct Abort   {};

using G_2PC = Transmission<Coord, Follower, Prepare,
              Transmission<Follower, Coord, Vote,
              Choice<Coord, Follower,
                  BranchG<Commit, End_G>,
                  BranchG<Abort,  End_G>>>>;

// Reflexive association.
static_assert(is_associated_v<
    projected_context_t<G_2PC, My2PC>, G_2PC, My2PC>);

// Refined: Coord narrows Select to commit-only.
using RefinedCoord = Send<Prepare,
                     Recv<Vote,
                     Select<Send<Commit, End>>>>;

using RefinedGamma = Context<
    Entry<My2PC, Coord,    RefinedCoord>,
    Entry<My2PC, Follower, project_t<G_2PC, Follower>>>;

static_assert(is_associated_v<RefinedGamma, G_2PC, My2PC>);

// ── Multi-session Γ: sessions independently associated ─────────

struct OtherSession {};

using MultiSessionGamma = Context<
    Entry<My2PC,        Coord,    project_t<G_2PC, Coord>>,
    Entry<My2PC,        Follower, project_t<G_2PC, Follower>>,
    Entry<OtherSession, Producer, ProducerProto>,
    Entry<OtherSession, Consumer, ConsumerProto>>;

// Each session associates with its own global type independently.
static_assert(is_associated_v<MultiSessionGamma, G_2PC,   My2PC>);
static_assert(is_associated_v<MultiSessionGamma, G_trace, OtherSession>);

// ── Runtime smoke ─────────────────────────────────────────────

int run_smoke() {
    if (context_size_v<ReflexiveGamma> != 2) return 1;
    if (context_size_v<RefinedGamma>   != 2) return 1;
    if (context_size_v<MultiSessionGamma> != 4) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_smoke();                       rc != 0) return rc;
    if (int rc = run_trace_via_context_lookup();    rc != 0) return rc;
    std::puts("session_assoc: Δ ⊑_s G + reflexive + refined + multi-session OK");
    return 0;
}
