#include <crucible/sessions/SessionDeclassify.h>

#include <cstdio>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

struct AuthToken {
    int user_id = 0;
    long timestamp = 0;
};

struct AuthAck {
    bool granted = false;
};

using TokenWire = DeclassifyOnSend<AuthToken, secret_policy::WireSerialize>;

using TokenAudit = DeclassifyOnSend<AuthToken, secret_policy::AuditedLogging>;

static_assert(is_declassify_on_send_v<TokenWire>);
static_assert(!is_declassify_on_send_v<AuthToken>);
static_assert(!is_declassify_on_send_v<Secret<AuthToken>>);

static_assert(DeclassifyOnSendable<TokenWire>);
static_assert(!DeclassifyOnSendable<AuthToken>);

static_assert(std::is_same_v<wire_payload_type_t<TokenWire>, AuthToken>);
static_assert(std::is_same_v<wire_policy_t<TokenWire>, secret_policy::WireSerialize>);

static_assert(!std::is_same_v<TokenWire, TokenAudit>);

static_assert(!std::is_copy_constructible_v<TokenWire>);
static_assert(std::is_move_constructible_v<TokenWire>);

static_assert(sizeof(TokenWire) == sizeof(AuthToken));

// The wrapper must not flow into a bare payload position.  Auditing which
// values reach the wire is a search for the wrapper's name, and that
// search stops being sound the moment the type system strips it.

static_assert(!is_subtype_sync_v<Send<TokenWire, End>, Send<AuthToken, End>>);

static_assert(!is_subtype_sync_v<Send<AuthToken, End>, Send<TokenWire, End>>);

static_assert(!is_subtype_sync_v<Recv<TokenWire, End>, Recv<AuthToken, End>>);

static_assert(!is_subtype_sync_v<Recv<AuthToken, End>, Recv<TokenWire, End>>);

static_assert(!is_subtype_sync_v<Send<TokenWire, End>, Send<TokenAudit, End>>);

int run_construct_from_secret() {
    Secret<AuthToken> s{AuthToken{42, 1700000000L}};
    TokenWire wrapped{std::move(s)};

    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw.user_id != 42) return 1;
    if (raw.timestamp != 1700000000L) return 2;
    return 0;
}

int run_construct_from_raw() {
    TokenWire wrapped{AuthToken{99, 1700000001L}};

    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw.user_id != 99) return 1;
    if (raw.timestamp != 1700000001L) return 2;
    return 0;
}

int run_construct_in_place() {
    TokenWire wrapped{std::in_place, 7, 1700000007L};

    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw.user_id != 7) return 1;
    if (raw.timestamp != 1700000007L) return 2;
    return 0;
}

int run_size_accessor() {
    using PasswordWire = DeclassifyOnSend<std::string, secret_policy::WireSerialize>;
    PasswordWire wrapped{std::string{"hunter2"}};

    if (wrapped.size() != 7) return 1;

    // Reading the size does not consume the wrapper.
    auto raw = std::move(wrapped).declassify_for_wire();
    if (raw != "hunter2") return 2;
    return 0;
}

// An authentication handshake, where the protocol states the wire
// classification in its own type and the transport holds the only
// declassification point.

using AuthHandshake = Send<TokenWire, Recv<AuthAck, End>>;

struct MockChannel {
    std::string written_token_user_id;
    AuthAck response_to_send{true};
};

int run_worked_example_auth_handshake() {
    MockChannel channel;
    auto handle = mint_session_handle<AuthHandshake>(&channel);

    auto next = std::move(handle).send(TokenWire{AuthToken{123, 1700001234L}},
                                       [](MockChannel*& ch, TokenWire&& payload) noexcept {
                                           // The policy travels in the wrapper's type, so no call site
                                           // can select the wrong one.
                                           auto raw = std::move(payload).declassify_for_wire();
                                           // Stands in for serialization.
                                           ch->written_token_user_id = std::to_string(raw.user_id);
                                       });

    if (channel.written_token_user_id != "123") return 1;

    auto [ack, end_handle] =
        std::move(next).recv([](MockChannel*& ch) noexcept -> AuthAck { return ch->response_to_send; });

    if (!ack.granted) return 2;

    auto* recovered = std::move(end_handle).close();
    if (recovered != &channel) return 3;
    return 0;
}

int run_trait_extraction() {
    using H = decltype(mint_session_handle<AuthHandshake>(std::declval<MockChannel*>()));

    using HMsg = typename H::message_type;
    static_assert(std::is_same_v<HMsg, TokenWire>);

    static_assert(std::is_same_v<wire_payload_type_t<HMsg>, AuthToken>);

    static_assert(std::is_same_v<wire_policy_t<HMsg>, secret_policy::WireSerialize>);

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_construct_from_secret(); rc != 0) return rc;
    if (int rc = run_construct_from_raw(); rc != 0) return 100 + rc;
    if (int rc = run_construct_in_place(); rc != 0) return 200 + rc;
    if (int rc = run_size_accessor(); rc != 0) return 300 + rc;
    if (int rc = run_worked_example_auth_handshake(); rc != 0) return 400 + rc;
    if (int rc = run_trait_extraction(); rc != 0) return 500 + rc;

    std::puts("session_declassify: payload wrapper + chokepoint declassify + auth handshake OK");
    return 0;
}
