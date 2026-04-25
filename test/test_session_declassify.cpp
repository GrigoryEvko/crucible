// Runtime + compile-time harness for safety/SessionDeclassify.h
// (task #402, SAFEINT-B13).
//
// Coverage:
//   * Compile-time: shape predicates (is_declassify_on_send_v),
//     concept (DeclassifyOnSendable), trait extraction
//     (wire_payload_type_t, wire_policy_t), move-only discipline,
//     sizeof equals wrapped Secret<T>.
//   * Compile-time: protocol-level subsort asymmetry — Send<Wrapped, K>
//     is NOT a subtype of Send<Bare, K> (audit-discoverability is
//     load-bearing); same for Recv contravariance.
//   * Runtime: construct from Secret<T>, raw T, in-place;
//     declassify_for_wire() yields the underlying T; size accessor
//     (when T has .size()); the typed-transport pattern from §13.
//   * Worked example: Vessel-style auth handshake — sender wraps
//     a credential in DeclassifyOnSend<AuthToken, WireSerialize>,
//     transport calls declassify_for_wire() at the SINGLE chokepoint,
//     receiver gets the bytes.

#include <crucible/sessions/SessionDeclassify.h>

#include <cstdio>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

// ── Fixture types ──────────────────────────────────────────────────

struct AuthToken {
    int  user_id   = 0;
    long timestamp = 0;
};

struct AuthAck {
    bool granted = false;
};

using TokenWire =
    DeclassifyOnSend<AuthToken, secret_policy::WireSerialize>;

using TokenAudit =
    DeclassifyOnSend<AuthToken, secret_policy::AuditedLogging>;

// ── Compile-time witnesses (mirrored TU-side) ─────────────────────

static_assert( is_declassify_on_send_v<TokenWire>);
static_assert(!is_declassify_on_send_v<AuthToken>);
static_assert(!is_declassify_on_send_v<Secret<AuthToken>>);

static_assert( DeclassifyOnSendable<TokenWire>);
static_assert(!DeclassifyOnSendable<AuthToken>);

static_assert(std::is_same_v<wire_payload_type_t<TokenWire>, AuthToken>);
static_assert(std::is_same_v<wire_policy_t<TokenWire>,
                              secret_policy::WireSerialize>);

// Different policies on the same T are distinct types.
static_assert(!std::is_same_v<TokenWire, TokenAudit>);

// Move-only discipline.
static_assert(!std::is_copy_constructible_v<TokenWire>);
static_assert( std::is_move_constructible_v<TokenWire>);

// Zero-cost size guarantee.
static_assert(sizeof(TokenWire) == sizeof(AuthToken));

// ── Compile-time: load-bearing subsort asymmetry ──────────────────
//
// DeclassifyOnSend<T, P> must NOT silently flow to bare T at any
// protocol position — the audit-grep `DeclassifyOnSend<` would lose
// its discoverability if the type system stripped the wrapper.

static_assert(!is_subtype_sync_v<
    Send<TokenWire, End>,
    Send<AuthToken, End>>);

static_assert(!is_subtype_sync_v<
    Send<AuthToken, End>,
    Send<TokenWire, End>>);

static_assert(!is_subtype_sync_v<
    Recv<TokenWire, End>,
    Recv<AuthToken, End>>);

static_assert(!is_subtype_sync_v<
    Recv<AuthToken, End>,
    Recv<TokenWire, End>>);

// Different policies are unrelated payloads.
static_assert(!is_subtype_sync_v<
    Send<TokenWire,  End>,
    Send<TokenAudit, End>>);

// ── Runtime: construct from Secret<T> ─────────────────────────────

int run_construct_from_secret() {
    Secret<AuthToken> s{AuthToken{42, 1700000000L}};
    TokenWire wrapped{std::move(s)};

    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw.user_id != 42)         return 1;
    if (raw.timestamp != 1700000000L) return 2;
    return 0;
}

// ── Runtime: construct from raw T ─────────────────────────────────

int run_construct_from_raw() {
    TokenWire wrapped{AuthToken{99, 1700000001L}};

    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw.user_id != 99)         return 1;
    if (raw.timestamp != 1700000001L) return 2;
    return 0;
}

// ── Runtime: in-place construction ────────────────────────────────

int run_construct_in_place() {
    TokenWire wrapped{std::in_place, 7, 1700000007L};

    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw.user_id != 7)          return 1;
    if (raw.timestamp != 1700000007L) return 2;
    return 0;
}

// ── Runtime: size accessor through Secret on string-like payloads ─

int run_size_accessor() {
    using PasswordWire = DeclassifyOnSend<std::string, secret_policy::WireSerialize>;
    PasswordWire wrapped{std::string{"hunter2"}};

    if (wrapped.size() != 7) return 1;

    // declassify still works after size() (size is non-consuming).
    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw != "hunter2") return 2;
    return 0;
}

// ── Worked example: Vessel-style auth handshake ───────────────────
//
// Protocol declares wire-classification at the type level.  The
// transport's declassify_for_wire() call is the SINGLE chokepoint.

using AuthHandshake = Send<TokenWire, Recv<AuthAck, End>>;

// A mock channel with a wire-buffer.
struct MockChannel {
    std::string written_token_user_id;  // serialized form
    AuthAck    response_to_send{true};
};

int run_worked_example_auth_handshake() {
    MockChannel channel;
    auto handle = make_session_handle<AuthHandshake>(&channel);

    // Sender side: construct the classified payload at the call
    // site, hand it to the transport which extracts the bytes via
    // the SINGLE chokepoint.
    auto next = std::move(handle).send(
        TokenWire{AuthToken{123, 1700001234L}},
        [](MockChannel*& ch, TokenWire&& payload) noexcept {
            // The audit-grep-able declassification.  Policy is
            // implicit in the wrapper's type; can't pick the
            // wrong one at the call site.
            auto raw = std::move(payload).declassify_for_wire();
            // Pretend to serialize: just record the user_id digits.
            ch->written_token_user_id = std::to_string(raw.user_id);
        });

    if (channel.written_token_user_id != "123") return 1;

    // Receiver side: receive the AuthAck.
    auto [ack, end_handle] = std::move(next).recv(
        [](MockChannel*& ch) noexcept -> AuthAck {
            return ch->response_to_send;
        });

    if (!ack.granted) return 2;

    // Clean termination.
    auto* recovered = std::move(end_handle).close();
    if (recovered != &channel) return 3;
    return 0;
}

// ── Runtime: trait extraction from a session-typed handle ─────────

int run_trait_extraction() {
    using H = decltype(make_session_handle<AuthHandshake>(
        std::declval<MockChannel*>()));

    // The handle's compile-time Proto is `Send<TokenWire, Recv<AuthAck, End>>`.
    using HMsg = typename H::message_type;
    static_assert(std::is_same_v<HMsg, TokenWire>);

    // wire_payload_type_t extracts AuthToken (the underlying T).
    static_assert(std::is_same_v<wire_payload_type_t<HMsg>, AuthToken>);

    // wire_policy_t extracts WireSerialize.
    static_assert(std::is_same_v<wire_policy_t<HMsg>,
                                  secret_policy::WireSerialize>);

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_construct_from_secret();           rc != 0) return rc;
    if (int rc = run_construct_from_raw();              rc != 0) return 100 + rc;
    if (int rc = run_construct_in_place();              rc != 0) return 200 + rc;
    if (int rc = run_size_accessor();                   rc != 0) return 300 + rc;
    if (int rc = run_worked_example_auth_handshake();   rc != 0) return 400 + rc;
    if (int rc = run_trait_extraction();                rc != 0) return 500 + rc;

    std::puts("session_declassify: payload wrapper + chokepoint declassify + auth handshake OK");
    return 0;
}
