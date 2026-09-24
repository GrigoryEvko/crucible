// Association is mostly covered by static_asserts inside the headers.
// This file drives it end to end: one global type, the context projected
// from it, a refined context, and a run of the projected protocol on
// handles whose types came out of the context by lookup.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionAssoc.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionMint.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

struct TraceSession {};
struct Producer {};
struct Consumer {};

struct Event {
    int sequence;
};

using G_trace = Rec_G<Transmission<Producer, Consumer, Event, Var_G>>;

using ProducerProto = project_t<G_trace, Producer>;
using ConsumerProto = project_t<G_trace, Consumer>;

using ReflexiveGamma = projected_context_t<G_trace, TraceSession>;

static_assert(is_associated_v<ReflexiveGamma, G_trace, TraceSession>);
static_assert(AssociatedWith<ReflexiveGamma, G_trace, TraceSession>);

static_assert(std::is_same_v<lookup_context_t<ReflexiveGamma, TraceSession, Producer>, ProducerProto>);
static_assert(std::is_same_v<lookup_context_t<ReflexiveGamma, TraceSession, Consumer>, ConsumerProto>);

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

auto send_event = [](Wire& w, Event&& e) noexcept { w.bytes->push_back("EV:" + std::to_string(e.sequence)); };
auto recv_event = [](Wire& w) noexcept -> Event {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return Event{std::atoi(s.data() + 3)};
};

int run_trace_via_context_lookup() {
    std::deque<std::string> wire;
    Wire p_res{&wire};
    Wire c_res{&wire};

    // The handle protocols come out of the context by lookup rather than
    // being written out by hand. That is the whole point: one global type,
    // one context, and every role's handle type falls out of it.
    using ProducerLocal = lookup_context_t<ReflexiveGamma, TraceSession, Producer>;
    using ConsumerLocal = lookup_context_t<ReflexiveGamma, TraceSession, Consumer>;
    crucible::effects::HotFgCtx ctx{};

    // For a two-role context over a binary global type, the lookup of one
    // role yields the dual of the other.
    static_assert(std::is_same_v<dual_of_t<ProducerLocal>, ConsumerLocal>);

    auto [prod, cons] = mint_channel<ProducerLocal>(ctx, ctx, std::move(p_res), std::move(c_res));

    auto p1 = std::move(prod).send(Event{0}, send_event);
    auto p2 = std::move(p1).send(Event{1}, send_event);
    auto p3 = std::move(p2).send(Event{2}, send_event);

    auto [e0, c1] = std::move(cons).recv(recv_event);
    auto [e1, c2] = std::move(c1).recv(recv_event);
    auto [e2, c3] = std::move(c2).recv(recv_event);

    if (e0.sequence != 0 || e1.sequence != 1 || e2.sequence != 2) {
        std::fprintf(stderr, "assoc: event ordering broken (%d, %d, %d)\n", e0.sequence, e1.sequence, e2.sequence);
        return 1;
    }

    // Both handles sit in a loop state with no close branch, so there is no
    // way to finish the protocol. Detaching is deliberate. Without it the
    // abandonment check fires in a debug build.
    std::move(p3).detach(detach_reason::InfiniteLoopProtocol{});
    std::move(c3).detach(detach_reason::InfiniteLoopProtocol{});
    return 0;
}

// The coordinator here commits unconditionally, offering one branch where
// the global type offers two. The narrowed context still associates.

struct My2PC {};
struct Coord {};
struct Follower {};
struct Prepare {};
struct Vote {};
struct Commit {};
struct Abort {};

using G_2PC = Transmission<
    Coord, Follower, Prepare,
    Transmission<Follower, Coord, Vote, Choice<Coord, Follower, BranchG<Commit, End_G>, BranchG<Abort, End_G>>>>;

static_assert(is_associated_v<projected_context_t<G_2PC, My2PC>, G_2PC, My2PC>);

using RefinedCoord = Send<Prepare, Recv<Vote, Select<Send<Commit, End>>>>;

using RefinedGamma = Context<Entry<My2PC, Coord, RefinedCoord>, Entry<My2PC, Follower, project_t<G_2PC, Follower>>>;

static_assert(is_associated_v<RefinedGamma, G_2PC, My2PC>);

struct OtherSession {};

using MultiSessionGamma =
    Context<Entry<My2PC, Coord, project_t<G_2PC, Coord>>, Entry<My2PC, Follower, project_t<G_2PC, Follower>>,
            Entry<OtherSession, Producer, ProducerProto>, Entry<OtherSession, Consumer, ConsumerProto>>;

// Each session associates with its own global type independently.
static_assert(is_associated_v<MultiSessionGamma, G_2PC, My2PC>);
static_assert(is_associated_v<MultiSessionGamma, G_trace, OtherSession>);

int run_smoke() {
    if (context_size_v<ReflexiveGamma> != 2) return 1;
    if (context_size_v<RefinedGamma> != 2) return 1;
    if (context_size_v<MultiSessionGamma> != 4) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_smoke(); rc != 0) return rc;
    if (int rc = run_trace_via_context_lookup(); rc != 0) return rc;
    std::puts("session_assoc: Δ ⊑_s G + reflexive + refined + multi-session OK");
    return 0;
}
