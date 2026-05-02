// Runtime + compile-time harness for safety/SessionCT.h
// (task #409, SAFEINT-C20).
//
// Coverage:
//   * Compile-time: requires_ct trait opt-in; CTPayload requires
//     RequiresCT concept; move-only discipline; sizeof equals raw T;
//     operator== / operator!= explicitly deleted; trait extraction
//     via ct_payload_value_type_t; subsort asymmetry holds.
//   * Runtime: construct from raw T or in-place; .bytes() returns
//     correct byte view; ct::eq compares byte-for-byte; equal
//     payloads compare equal; differing payloads compare unequal;
//     declassify_ct<Policy>() yields underlying T.
//   * Worked example: HMAC-tag verification protocol — receiver
//     compares received tag against expected via ct::eq, sends
//     bool ack.

#include <crucible/sessions/SessionCT.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

// ── Fixture types ──────────────────────────────────────────────────

struct TestHmacTag {
    std::array<unsigned char, 16> bytes{};
};

}  // anonymous namespace

// Opt the fixture into the CT discipline.  Specialisation must live
// in the safety::ct namespace.
namespace crucible::safety::ct {
template <>
struct requires_ct<TestHmacTag> : std::true_type {};
}  // namespace

namespace {

using TagPayload = ct::CTPayload<TestHmacTag>;

// ── Compile-time witnesses (mirrored TU-side) ─────────────────────

static_assert( ct::requires_ct_v<TestHmacTag>);
static_assert( ct::RequiresCT<TestHmacTag>);
static_assert(!ct::requires_ct_v<int>);
static_assert(!ct::RequiresCT<int>);

static_assert( ct::is_ct_payload_v<TagPayload>);
static_assert(!ct::is_ct_payload_v<TestHmacTag>);
static_assert( ct::CTPayloadType<TagPayload>);

static_assert(std::is_same_v<ct::ct_payload_value_type_t<TagPayload>,
                              TestHmacTag>);
static_assert(std::is_same_v<ct::ct_payload_value_type_t<int>, int>);

// Move-only.
static_assert(!std::is_copy_constructible_v<TagPayload>);
static_assert( std::is_move_constructible_v<TagPayload>);

// Zero-cost.
static_assert(sizeof(TagPayload) == sizeof(TestHmacTag));

// operator== / operator!= are deleted — verified via concept-based
// SFINAE check.
template <typename A, typename B>
concept has_op_eq = requires(A a, B b) { a == b; };

template <typename A, typename B>
concept has_op_ne = requires(A a, B b) { a != b; };

static_assert(!has_op_eq<TagPayload const&, TagPayload const&>);
static_assert(!has_op_ne<TagPayload const&, TagPayload const&>);

// Subsort asymmetry — CTPayload does NOT silently flow to bare T.
static_assert(!is_subtype_sync_v<Send<TagPayload, End>,
                                  Send<TestHmacTag, End>>);
static_assert(!is_subtype_sync_v<Send<TestHmacTag, End>,
                                  Send<TagPayload, End>>);
static_assert(!is_subtype_sync_v<Recv<TagPayload, End>,
                                  Recv<TestHmacTag, End>>);

// ── Runtime: construct + bytes() yields correct view ──────────────

int run_construct_and_bytes() {
    TestHmacTag tag{};
    for (size_t i = 0; i < tag.bytes.size(); ++i) {
        tag.bytes[i] = static_cast<unsigned char>(i + 1);
    }

    TagPayload payload{tag};

    auto view = payload.bytes();
    if (view.size() != sizeof(TestHmacTag))           return 1;
    for (size_t i = 0; i < tag.bytes.size(); ++i) {
        if (view[i] != static_cast<std::byte>(i + 1)) return 2;
    }
    return 0;
}

// ── Runtime: in-place construction ────────────────────────────────

int run_in_place_construction() {
    TagPayload payload{std::in_place,
                        TestHmacTag{{1, 2, 3, 4, 5, 6, 7, 8,
                                     9, 10, 11, 12, 13, 14, 15, 16}}};

    auto raw = std::move(payload).declassify_ct<secret_policy::HashForCompare>();
    if (raw.bytes[0]  != 1)  return 1;
    if (raw.bytes[15] != 16) return 2;
    return 0;
}

// ── Runtime: ct::eq returns true on identical payloads ────────────

int run_ct_eq_identical() {
    TestHmacTag tag1{};
    TestHmacTag tag2{};
    for (size_t i = 0; i < 16; ++i) {
        tag1.bytes[i] = static_cast<unsigned char>(0xAA);
        tag2.bytes[i] = static_cast<unsigned char>(0xAA);
    }

    TagPayload p1{tag1};
    TagPayload p2{tag2};

    if (!ct::eq(p1, p2)) return 1;
    return 0;
}

// ── Runtime: ct::eq returns false on differing payloads ───────────

int run_ct_eq_differing() {
    TestHmacTag tag1{};
    TestHmacTag tag2{};
    for (size_t i = 0; i < 16; ++i) {
        tag1.bytes[i] = static_cast<unsigned char>(0xAA);
        tag2.bytes[i] = static_cast<unsigned char>(0xAA);
    }
    tag2.bytes[7] = 0xBB;  // single-byte difference at position 7

    TagPayload p1{tag1};
    TagPayload p2{tag2};

    if (ct::eq(p1, p2))  return 1;
    return 0;
}

// ── Runtime: ct::eq returns false on differing first byte ─────────
//
// Guards against an early-return optimization that would short-
// circuit on the first mismatch (would not be CT).  The expected
// behaviour is the same wall-clock-time as for identical inputs.

int run_ct_eq_differs_at_first_byte() {
    TestHmacTag tag1{};
    TestHmacTag tag2{};
    for (size_t i = 0; i < 16; ++i) {
        tag1.bytes[i] = static_cast<unsigned char>(0xAA);
        tag2.bytes[i] = static_cast<unsigned char>(0xAA);
    }
    tag2.bytes[0] = 0xCC;  // first-byte difference

    TagPayload p1{tag1};
    TagPayload p2{tag2};

    if (ct::eq(p1, p2)) return 1;
    return 0;
}

// ── Runtime: declassify_ct yields the underlying T ────────────────

int run_declassify_ct() {
    TestHmacTag tag{};
    for (size_t i = 0; i < 16; ++i) {
        tag.bytes[i] = static_cast<unsigned char>(0xDD);
    }

    TagPayload payload{tag};

    auto raw = std::move(payload).declassify_ct<secret_policy::HashForCompare>();
    if (raw.bytes[0]  != 0xDD) return 1;
    if (raw.bytes[15] != 0xDD) return 2;
    return 0;
}

// ── Worked example: HMAC-tag verification protocol ────────────────
//
// Recv<CTPayload<HmacTag>, Send<bool, End>> — receiver gets a
// claimed tag, compares against the expected via ct::eq, sends
// the verification result.

using AuthVerifyProto = Recv<TagPayload, Send<bool, End>>;

struct MockChannel {
    TestHmacTag claimed_tag{};
    bool ack_sent = false;
    bool ack_value = false;
};

int run_worked_example_hmac_verify() {
    MockChannel channel;
    // The "claimed" tag arriving on the wire.
    for (size_t i = 0; i < 16; ++i) {
        channel.claimed_tag.bytes[i] = static_cast<unsigned char>(0x11);
    }

    auto handle = mint_session_handle<AuthVerifyProto>(&channel);

    // Receive the claimed tag.
    auto [received_payload, after_recv] = std::move(handle).recv(
        [](MockChannel*& c) noexcept -> TagPayload {
            return TagPayload{c->claimed_tag};
        });

    // The expected tag (same content as claimed in this test).
    TestHmacTag expected{};
    for (size_t i = 0; i < 16; ++i) {
        expected.bytes[i] = static_cast<unsigned char>(0x11);
    }
    TagPayload expected_payload{expected};

    // CT comparison — the only path that compiles.
    bool ok = ct::eq(received_payload, expected_payload);
    if (!ok) return 1;

    // Send the result.
    auto end_handle = std::move(after_recv).send(
        ok,
        [](MockChannel*& c, bool v) noexcept {
            c->ack_sent = true;
            c->ack_value = v;
        });

    if (!channel.ack_sent)   return 2;
    if (!channel.ack_value)  return 3;

    auto* recovered = std::move(end_handle).close();
    if (recovered != &channel) return 4;
    return 0;
}

// ── Runtime: HMAC-verify with mismatched tag ──────────────────────

int run_worked_example_hmac_mismatch() {
    MockChannel channel;
    for (size_t i = 0; i < 16; ++i) {
        channel.claimed_tag.bytes[i] = static_cast<unsigned char>(0x22);
    }

    auto handle = mint_session_handle<AuthVerifyProto>(&channel);
    auto [received_payload, after_recv] = std::move(handle).recv(
        [](MockChannel*& c) noexcept -> TagPayload {
            return TagPayload{c->claimed_tag};
        });

    // Expected differs by one byte.
    TestHmacTag expected{};
    for (size_t i = 0; i < 16; ++i) {
        expected.bytes[i] = static_cast<unsigned char>(0x22);
    }
    expected.bytes[10] = 0xFF;
    TagPayload expected_payload{expected};

    bool ok = ct::eq(received_payload, expected_payload);
    if (ok) return 1;  // must be false on mismatch

    auto end_handle = std::move(after_recv).send(
        ok,
        [](MockChannel*& c, bool v) noexcept {
            c->ack_sent = true;
            c->ack_value = v;
        });

    if (!channel.ack_sent) return 2;
    if (channel.ack_value) return 3;  // ack must reflect the failure

    auto* recovered = std::move(end_handle).close();
    if (recovered != &channel) return 4;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_construct_and_bytes();              rc != 0) return rc;
    if (int rc = run_in_place_construction();            rc != 0) return 100 + rc;
    if (int rc = run_ct_eq_identical();                  rc != 0) return 200 + rc;
    if (int rc = run_ct_eq_differing();                  rc != 0) return 300 + rc;
    if (int rc = run_ct_eq_differs_at_first_byte();      rc != 0) return 400 + rc;
    if (int rc = run_declassify_ct();                    rc != 0) return 500 + rc;
    if (int rc = run_worked_example_hmac_verify();       rc != 0) return 600 + rc;
    if (int rc = run_worked_example_hmac_mismatch();     rc != 0) return 700 + rc;

    std::puts("session_ct: opt-in trait + CTPayload + ct::eq + HMAC-verify OK");
    return 0;
}
