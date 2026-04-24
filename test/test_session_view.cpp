// Runtime + compile-time harness for safety/SessionView.h
// (task #405, SAFEINT-B16 from misc/24_04_2026_safety_integration.md
// §16).
//
// Coverage:
//   * Compile-time: handle_is_at_v positive + negative for every
//     SessionHandle specialisation × every tag; view_ok ADL hook
//     evaluates to handle_is_at_v; mint_session_view's requires-
//     clause rejects wrong-position requests at the factory.
//   * Runtime: mint a view at each combinator-head position; verify
//     view->resource() / view->protocol_name() / view.carrier()
//     work; verify the handle remains usable after the view is
//     dropped (non-consuming inspection); verify multiple views
//     of the same handle can coexist (read-only borrows).
//   * Worked example: a "metrics broadcast" callback that takes a
//     position-tagged view as proof and reports protocol_name +
//     branch count; only callable when handle is at the matching
//     position (compile-time enforced).

#include <crucible/safety/SessionView.h>

#include <cstdio>
#include <string_view>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

struct FakeRes { int sentinel = 42; };
struct Msg     {};
struct Other   {};

// ── Compile-time: position-tag predicates (mirror the in-header
//    self-tests for TU-side regression visibility) ────────────────────

static_assert( handle_is_at_v<SessionHandle<Send<Msg, End>,           FakeRes>, AtSend>);
static_assert( handle_is_at_v<SessionHandle<Recv<Msg, End>,           FakeRes>, AtRecv>);
static_assert( handle_is_at_v<SessionHandle<Select<Send<Msg, End>>,   FakeRes>, AtSelect>);
static_assert( handle_is_at_v<SessionHandle<Offer<Recv<Msg, End>>,    FakeRes>, AtOffer>);
static_assert( handle_is_at_v<SessionHandle<End,                      FakeRes>, AtEnd>);
static_assert( handle_is_at_v<SessionHandle<Stop,                     FakeRes>, AtStop>);
static_assert( handle_is_at_v<SessionHandle<End,                      FakeRes>, AtTerminal>);
static_assert( handle_is_at_v<SessionHandle<Stop,                     FakeRes>, AtTerminal>);

// Wrong-position requests rejected.
static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);
static_assert(!handle_is_at_v<SessionHandle<End,            FakeRes>, AtSend>);
static_assert(!handle_is_at_v<SessionHandle<Stop,           FakeRes>, AtEnd>);
static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtTerminal>);

// Concept gate: mint_session_view requires HandleIsAt.
static_assert( HandleIsAt<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>);
static_assert(!HandleIsAt<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);

// ── Compile-time: view_message_type / view_branch_count helpers ───

using SendView_t = ScopedView<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>;
static_assert(std::is_same_v<session_view_message_type_t<SendView_t>, Msg>);

using RecvView_t = ScopedView<SessionHandle<Recv<Other, End>, FakeRes>, AtRecv>;
static_assert(std::is_same_v<session_view_message_type_t<RecvView_t>, Other>);

using SelectView_t = ScopedView<SessionHandle<Select<Send<Msg, End>, End>, FakeRes>,
                                 AtSelect>;
static_assert(session_view_branch_count_v<SelectView_t> == 2);

using OfferView_t = ScopedView<SessionHandle<Offer<Recv<Msg, End>, End, End>, FakeRes>,
                                AtOffer>;
static_assert(session_view_branch_count_v<OfferView_t> == 3);

// ── Runtime: mint a view at each combinator-head position ──────────

int run_mint_at_send() {
    auto h = make_session_handle<Send<Msg, End>>(FakeRes{99});

    auto view = mint_session_view<AtSend>(h);
    if (view->resource().sentinel != 99) return 1;

    // The handle remains usable; consume it via send().
    int side_effect = 0;
    auto next = std::move(h).send(
        Msg{},
        [&side_effect](FakeRes&, Msg) noexcept { side_effect = 1; });
    if (side_effect != 1) return 2;

    // next is at End; consume cleanly.
    auto recovered = std::move(next).close();
    if (recovered.sentinel != 99) return 3;

    return 0;
}

int run_mint_at_recv() {
    auto h = make_session_handle<Recv<Msg, End>>(FakeRes{77});

    auto view = mint_session_view<AtRecv>(h);
    if (view->resource().sentinel != 77) return 1;

    int side_effect = 0;
    auto [msg, next] = std::move(h).recv(
        [&side_effect](FakeRes&) noexcept -> Msg {
            side_effect = 1;
            return Msg{};
        });
    (void)msg;
    if (side_effect != 1) return 2;

    auto recovered = std::move(next).close();
    if (recovered.sentinel != 77) return 3;
    return 0;
}

int run_mint_at_select() {
    auto h = make_session_handle<Select<Send<Msg, End>, End>>(FakeRes{55});

    auto view = mint_session_view<AtSelect>(h);
    if (view->resource().sentinel != 55) return 1;
    if (SessionHandle<Select<Send<Msg, End>, End>, FakeRes>::branch_count != 2) return 2;

    // Pick branch 1 (the End branch) and consume.
    auto end_handle = std::move(h).select_local<1>();
    auto recovered = std::move(end_handle).close();
    if (recovered.sentinel != 55) return 3;
    return 0;
}

int run_mint_at_offer() {
    auto h = make_session_handle<Offer<Recv<Msg, End>, End>>(FakeRes{33});

    auto view = mint_session_view<AtOffer>(h);
    if (view->resource().sentinel != 33) return 1;
    if (SessionHandle<Offer<Recv<Msg, End>, End>, FakeRes>::branch_count != 2) return 2;

    auto end_handle = std::move(h).pick_local<1>();
    auto recovered  = std::move(end_handle).close();
    if (recovered.sentinel != 33) return 3;
    return 0;
}

int run_mint_at_end_and_terminal() {
    auto h = make_session_handle<End>(FakeRes{11});

    auto end_view = mint_session_view<AtEnd>(h);
    if (end_view->resource().sentinel != 11) return 1;

    // AtTerminal admits End too — same handle minted twice.
    auto term_view = mint_session_view<AtTerminal>(h);
    if (term_view->resource().sentinel != 11) return 2;

    auto recovered = std::move(h).close();
    if (recovered.sentinel != 11) return 3;
    return 0;
}

int run_mint_at_stop() {
    auto h = make_session_handle<Stop>(FakeRes{22});

    auto stop_view = mint_session_view<AtStop>(h);
    if (stop_view->resource().sentinel != 22) return 1;

    // Stop is also AtTerminal.
    auto term_view = mint_session_view<AtTerminal>(h);
    if (term_view->resource().sentinel != 22) return 2;

    // Stop's destructor is no-op (terminal); just let h go out of scope.
    return 0;
}

// ── Runtime: views are read-only borrows; multiple may coexist ────

int run_multiple_views_coexist() {
    auto h = make_session_handle<Send<Msg, End>>(FakeRes{88});

    auto v1 = mint_session_view<AtSend>(h);
    auto v2 = mint_session_view<AtSend>(h);   // second view of same handle
    auto v3 = mint_session_view<AtSend>(h);   // third

    // All three witness the same carrier.
    if (&v1.carrier() != &v2.carrier()) return 1;
    if (&v2.carrier() != &v3.carrier()) return 2;
    if (v1->resource().sentinel != 88)  return 3;
    if (v2->resource().sentinel != 88)  return 4;

    // Drop the views; consume the handle via send.
    int side_effect = 0;
    auto next = std::move(h).send(
        Msg{},
        [&side_effect](FakeRes&, Msg) noexcept { side_effect = 1; });
    (void)std::move(next).close();
    if (side_effect != 1) return 5;
    return 0;
}

// ── Runtime: protocol_name accessor through the view ──────────────

int run_view_protocol_name() {
    auto h = make_session_handle<Send<Msg, End>>(FakeRes{});
    auto view = mint_session_view<AtSend>(h);

    // session_view_protocol_name<View>() forwards to the carrier's
    // protocol_name() static accessor; same caveat about NOT
    // capturing into constexpr auto for runtime find().
    auto name = session_view_protocol_name<decltype(view)>();
    if (name.empty())                                  return 1;
    if (name.find("Send") == std::string_view::npos)   return 2;

    // Ensure the handle is still consumable after the view's use.
    auto next = std::move(h).send(
        Msg{},
        [](FakeRes&, Msg) noexcept {});
    (void)std::move(next).close();
    return 0;
}

// ── Worked example: typed metrics broadcast ────────────────────────
//
// A function that takes a position-tagged view as proof — only
// callable when the handle is at the matching position.  This is the
// pattern Augur and similar metrics-broadcast paths follow per §16.

template <typename Handle>
    requires HandleIsAt<Handle, AtRecv>
int report_pending_recv(ScopedView<Handle, AtRecv> view) noexcept {
    auto name = session_view_protocol_name<decltype(view)>();
    if (name.find("Recv") == std::string_view::npos) return 1;
    if (view->resource().sentinel != 999)            return 2;
    return 0;
}

int run_worked_example_typed_metrics() {
    auto h = make_session_handle<Recv<Msg, End>>(FakeRes{999});

    auto view = mint_session_view<AtRecv>(h);
    if (int rc = report_pending_recv(view); rc != 0) return rc;

    // Handle still alive — consume cleanly.
    auto [msg, next] = std::move(h).recv(
        [](FakeRes&) noexcept -> Msg { return Msg{}; });
    (void)msg;
    auto recovered = std::move(next).close();
    if (recovered.sentinel != 999) return 9;
    return 0;
}

// ── Runtime: composes with Loop's body — handle is at the body's
//    head, not at Loop ──────────────────────────────────────────────

int run_loop_body_position() {
    // Loop<Send<Msg, Continue>> — make_session_handle unrolls one
    // iteration; the resulting handle's compile-time Proto is
    // Send<Msg, Continue>, with Loop<...> as LoopCtx.
    using P  = Loop<Send<Msg, Continue>>;
    using Body = Send<Msg, Continue>;
    auto h = make_session_handle<P>(FakeRes{44});

    // Verify the handle's compile-time type matches the unroll.
    static_assert(std::is_same_v<decltype(h),
                                  SessionHandle<Body, FakeRes, P>>);

    // Mint a view at AtSend — works because the unrolled body is
    // Send.  The LoopCtx is part of the Handle's signature but
    // does not participate in position dispatch.
    auto view = mint_session_view<AtSend>(h);
    if (view->resource().sentinel != 44) return 1;

    // Consume one iteration via send → returns a handle at Continue's
    // resolution, which step_to_next routes back to the body's head
    // (still at Send).  Detach to clean up.
    auto next = std::move(h).send(
        Msg{},
        [](FakeRes&, Msg) noexcept {});
    static_assert(std::is_same_v<decltype(next),
                                  SessionHandle<Body, FakeRes, P>>);
    std::move(next).detach(detach_reason::TestInstrumentation{});
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_mint_at_send();                  rc != 0) return rc;
    if (int rc = run_mint_at_recv();                  rc != 0) return 100 + rc;
    if (int rc = run_mint_at_select();                rc != 0) return 200 + rc;
    if (int rc = run_mint_at_offer();                 rc != 0) return 300 + rc;
    if (int rc = run_mint_at_end_and_terminal();      rc != 0) return 400 + rc;
    if (int rc = run_mint_at_stop();                  rc != 0) return 500 + rc;
    if (int rc = run_multiple_views_coexist();        rc != 0) return 600 + rc;
    if (int rc = run_view_protocol_name();            rc != 0) return 700 + rc;
    if (int rc = run_worked_example_typed_metrics();  rc != 0) return 800 + rc;
    if (int rc = run_loop_body_position();            rc != 0) return 900 + rc;

    std::puts("session_view: position tags + non-consuming views + typed metrics OK");
    return 0;
}
