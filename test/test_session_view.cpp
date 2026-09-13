#include <crucible/sessions/SessionView.h>

#include <cstdio>
#include <string_view>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

struct FakeRes {
    int sentinel = 42;
};
struct Msg {};
struct Other {};

// These mirror the header's own self-tests. Repeating them in a translation
// unit is what puts them under the project warning flags.

static_assert(handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>);
static_assert(handle_is_at_v<SessionHandle<Recv<Msg, End>, FakeRes>, AtRecv>);
static_assert(handle_is_at_v<SessionHandle<Select<Send<Msg, End>>, FakeRes>, AtSelect>);
static_assert(handle_is_at_v<SessionHandle<Offer<Recv<Msg, End>>, FakeRes>, AtOffer>);
static_assert(handle_is_at_v<SessionHandle<End, FakeRes>, AtEnd>);
static_assert(handle_is_at_v<SessionHandle<Stop, FakeRes>, AtStop>);
static_assert(handle_is_at_v<SessionHandle<End, FakeRes>, AtTerminal>);
static_assert(handle_is_at_v<SessionHandle<Stop, FakeRes>, AtTerminal>);

static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);
static_assert(!handle_is_at_v<SessionHandle<End, FakeRes>, AtSend>);
static_assert(!handle_is_at_v<SessionHandle<Stop, FakeRes>, AtEnd>);
static_assert(!handle_is_at_v<SessionHandle<Send<Msg, End>, FakeRes>, AtTerminal>);

static_assert(HandleIsAt<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>);
static_assert(!HandleIsAt<SessionHandle<Send<Msg, End>, FakeRes>, AtRecv>);

using SendView_t = ScopedView<SessionHandle<Send<Msg, End>, FakeRes>, AtSend>;
static_assert(std::is_same_v<session_view_message_type_t<SendView_t>, Msg>);

using RecvView_t = ScopedView<SessionHandle<Recv<Other, End>, FakeRes>, AtRecv>;
static_assert(std::is_same_v<session_view_message_type_t<RecvView_t>, Other>);

using SelectView_t = ScopedView<SessionHandle<Select<Send<Msg, End>, End>, FakeRes>, AtSelect>;
static_assert(session_view_branch_count_v<SelectView_t> == 2);

using OfferView_t = ScopedView<SessionHandle<Offer<Recv<Msg, End>, End, End>, FakeRes>, AtOffer>;
static_assert(session_view_branch_count_v<OfferView_t> == 3);

int run_mint_at_send() {
    auto h = mint_session_handle<Send<Msg, End>>(FakeRes{99});

    auto view = mint_session_view<AtSend>(h);
    if (view->resource().sentinel != 99) return 1;

    // The handle is still usable here, which is what makes the view a borrow
    // rather than a consumption.
    int side_effect = 0;
    auto next = std::move(h).send(Msg{}, [&side_effect](FakeRes&, Msg) noexcept { side_effect = 1; });
    if (side_effect != 1) return 2;

    auto recovered = std::move(next).close();
    if (recovered.sentinel != 99) return 3;

    return 0;
}

int run_mint_at_recv() {
    auto h = mint_session_handle<Recv<Msg, End>>(FakeRes{77});

    auto view = mint_session_view<AtRecv>(h);
    if (view->resource().sentinel != 77) return 1;

    int side_effect = 0;
    auto [msg, next] = std::move(h).recv([&side_effect](FakeRes&) noexcept -> Msg {
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
    auto h = mint_session_handle<Select<Send<Msg, End>, End>>(FakeRes{55});

    auto view = mint_session_view<AtSelect>(h);
    if (view->resource().sentinel != 55) return 1;
    if (SessionHandle<Select<Send<Msg, End>, End>, FakeRes>::branch_count != 2) return 2;

    auto end_handle = std::move(h).select_local<1>();
    auto recovered = std::move(end_handle).close();
    if (recovered.sentinel != 55) return 3;
    return 0;
}

int run_mint_at_offer() {
    auto h = mint_session_handle<Offer<Recv<Msg, End>, End>>(FakeRes{33});

    auto view = mint_session_view<AtOffer>(h);
    if (view->resource().sentinel != 33) return 1;
    if (SessionHandle<Offer<Recv<Msg, End>, End>, FakeRes>::branch_count != 2) return 2;

    auto end_handle = std::move(h).pick_local<1>();
    auto recovered = std::move(end_handle).close();
    if (recovered.sentinel != 33) return 3;
    return 0;
}

int run_mint_at_end_and_terminal() {
    auto h = mint_session_handle<End>(FakeRes{11});

    auto end_view = mint_session_view<AtEnd>(h);
    if (end_view->resource().sentinel != 11) return 1;

    // AtTerminal covers End as well as Stop, so the same handle is at two
    // positions at once.
    auto term_view = mint_session_view<AtTerminal>(h);
    if (term_view->resource().sentinel != 11) return 2;

    auto recovered = std::move(h).close();
    if (recovered.sentinel != 11) return 3;
    return 0;
}

int run_mint_at_stop() {
    auto h = mint_session_handle<Stop>(FakeRes{22});

    auto stop_view = mint_session_view<AtStop>(h);
    if (stop_view->resource().sentinel != 22) return 1;

    auto term_view = mint_session_view<AtTerminal>(h);
    if (term_view->resource().sentinel != 22) return 2;

    // Stop is terminal and its destructor does nothing, so there is no close()
    // to pair with the mint. Letting h leave scope is the whole cleanup.
    return 0;
}

int run_multiple_views_coexist() {
    auto h = mint_session_handle<Send<Msg, End>>(FakeRes{88});

    auto v1 = mint_session_view<AtSend>(h);
    auto v2 = mint_session_view<AtSend>(h);
    auto v3 = mint_session_view<AtSend>(h);

    if (&v1.carrier() != &v2.carrier()) return 1;
    if (&v2.carrier() != &v3.carrier()) return 2;
    if (v1->resource().sentinel != 88) return 3;
    if (v2->resource().sentinel != 88) return 4;

    int side_effect = 0;
    auto next = std::move(h).send(Msg{}, [&side_effect](FakeRes&, Msg) noexcept { side_effect = 1; });
    (void)std::move(next).close();
    if (side_effect != 1) return 5;
    return 0;
}

int run_view_protocol_name() {
    auto h = mint_session_handle<Send<Msg, End>>(FakeRes{});
    auto view = mint_session_view<AtSend>(h);

    // Bound to a plain local rather than a constexpr one, because the search
    // below runs at runtime against a spelling that varies with context.
    auto name = session_view_protocol_name<decltype(view)>();
    if (name.empty()) return 1;
    if (name.find("Send") == std::string_view::npos) return 2;

    auto next = std::move(h).send(Msg{}, [](FakeRes&, Msg) noexcept {});
    (void)std::move(next).close();
    return 0;
}

// The view is the argument, so the function is only callable while the handle
// sits at the matching position. That check happens where the call is written,
// not inside the body.

template <typename Handle>
    requires HandleIsAt<Handle, AtRecv>
int report_pending_recv(ScopedView<Handle, AtRecv> view) noexcept {
    auto name = session_view_protocol_name<decltype(view)>();
    if (name.find("Recv") == std::string_view::npos) return 1;
    if (view->resource().sentinel != 999) return 2;
    return 0;
}

int run_worked_example_typed_metrics() {
    auto h = mint_session_handle<Recv<Msg, End>>(FakeRes{999});

    auto view = mint_session_view<AtRecv>(h);
    if (int rc = report_pending_recv(view); rc != 0) return rc;

    auto [msg, next] = std::move(h).recv([](FakeRes&) noexcept -> Msg { return Msg{}; });
    (void)msg;
    auto recovered = std::move(next).close();
    if (recovered.sentinel != 999) return 9;
    return 0;
}

int run_loop_body_position() {
    // Minting a handle for a loop unrolls one iteration, so the handle's
    // protocol is the body and the loop itself is carried alongside as
    // context. That context takes no part in position dispatch.
    using P = Loop<Send<Msg, Continue>>;
    using Body = Send<Msg, Continue>;
    auto h = mint_session_handle<P>(FakeRes{44});

    static_assert(std::is_same_v<decltype(h), SessionHandle<Body, FakeRes, P>>);

    auto view = mint_session_view<AtSend>(h);
    if (view->resource().sentinel != 44) return 1;

    // One iteration lands back at the body's head rather than advancing past
    // the loop, so the type is unchanged and the handle needs detaching.
    auto next = std::move(h).send(Msg{}, [](FakeRes&, Msg) noexcept {});
    static_assert(std::is_same_v<decltype(next), SessionHandle<Body, FakeRes, P>>);
    std::move(next).detach(detach_reason::TestInstrumentation{});
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_mint_at_send(); rc != 0) return rc;
    if (int rc = run_mint_at_recv(); rc != 0) return 100 + rc;
    if (int rc = run_mint_at_select(); rc != 0) return 200 + rc;
    if (int rc = run_mint_at_offer(); rc != 0) return 300 + rc;
    if (int rc = run_mint_at_end_and_terminal(); rc != 0) return 400 + rc;
    if (int rc = run_mint_at_stop(); rc != 0) return 500 + rc;
    if (int rc = run_multiple_views_coexist(); rc != 0) return 600 + rc;
    if (int rc = run_view_protocol_name(); rc != 0) return 700 + rc;
    if (int rc = run_worked_example_typed_metrics(); rc != 0) return 800 + rc;
    if (int rc = run_loop_body_position(); rc != 0) return 900 + rc;

    std::puts("session_view: position tags + non-consuming views + typed metrics OK");
    return 0;
}
