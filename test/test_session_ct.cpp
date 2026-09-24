#include <crucible/sessions/SessionCT.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

struct TestHmacTag {
    std::array<unsigned char, 16> bytes{};
};

}  // anonymous namespace

// The opt-in specialization has to be written in the namespace that declares
// the trait, not in the one that declares the type.
namespace crucible::safety::ct {
template <>
struct requires_ct<TestHmacTag> : std::true_type {};
}  // namespace crucible::safety::ct

namespace {

using TagPayload = ct::CTPayload<TestHmacTag>;

static_assert(ct::requires_ct_v<TestHmacTag>);
static_assert(ct::RequiresCT<TestHmacTag>);
static_assert(!ct::requires_ct_v<int>);
static_assert(!ct::RequiresCT<int>);

static_assert(ct::is_ct_payload_v<TagPayload>);
static_assert(!ct::is_ct_payload_v<TestHmacTag>);
static_assert(ct::CTPayloadType<TagPayload>);

static_assert(std::is_same_v<ct::ct_payload_value_type_t<TagPayload>, TestHmacTag>);
static_assert(std::is_same_v<ct::ct_payload_value_type_t<int>, int>);

static_assert(!std::is_copy_constructible_v<TagPayload>);
static_assert(std::is_move_constructible_v<TagPayload>);

static_assert(sizeof(TagPayload) == sizeof(TestHmacTag));

// The comparison operators are deleted, which cannot be asserted directly.
// The concepts below detect their absence instead.
template <typename A, typename B>
concept has_op_eq = requires(A a, B b) { a == b; };

template <typename A, typename B>
concept has_op_ne = requires(A a, B b) { a != b; };

static_assert(!has_op_eq<TagPayload const&, TagPayload const&>);
static_assert(!has_op_ne<TagPayload const&, TagPayload const&>);

// The wrapper does not flow to the bare type in either direction, so a
// protocol cannot quietly drop the constant-time discipline through
// subtyping.
static_assert(!is_subtype_sync_v<Send<TagPayload, End>, Send<TestHmacTag, End>>);
static_assert(!is_subtype_sync_v<Send<TestHmacTag, End>, Send<TagPayload, End>>);
static_assert(!is_subtype_sync_v<Recv<TagPayload, End>, Recv<TestHmacTag, End>>);

int run_construct_and_bytes() {
    TestHmacTag tag{};
    for (size_t i = 0; i < tag.bytes.size(); ++i) {
        tag.bytes[i] = static_cast<unsigned char>(i + 1);
    }

    TagPayload payload{tag};

    auto view = payload.bytes();
    if (view.size() != sizeof(TestHmacTag)) return 1;
    for (size_t i = 0; i < tag.bytes.size(); ++i) {
        if (view[i] != static_cast<std::byte>(i + 1)) return 2;
    }
    return 0;
}

int run_in_place_construction() {
    TagPayload payload{std::in_place, TestHmacTag{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}}};

    auto raw = std::move(payload).declassify_ct<secret_policy::HashForCompare>();
    if (raw.bytes[0] != 1) return 1;
    if (raw.bytes[15] != 16) return 2;
    return 0;
}

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

int run_ct_eq_differing() {
    TestHmacTag tag1{};
    TestHmacTag tag2{};
    for (size_t i = 0; i < 16; ++i) {
        tag1.bytes[i] = static_cast<unsigned char>(0xAA);
        tag2.bytes[i] = static_cast<unsigned char>(0xAA);
    }
    tag2.bytes[7] = 0xBB;

    TagPayload p1{tag1};
    TagPayload p2{tag2};

    if (ct::eq(p1, p2)) return 1;
    return 0;
}

// A mismatch in the very first byte is the case an early-return
// implementation would answer fastest, and answering it faster is exactly what
// the comparison must not do.

int run_ct_eq_differs_at_first_byte() {
    TestHmacTag tag1{};
    TestHmacTag tag2{};
    for (size_t i = 0; i < 16; ++i) {
        tag1.bytes[i] = static_cast<unsigned char>(0xAA);
        tag2.bytes[i] = static_cast<unsigned char>(0xAA);
    }
    tag2.bytes[0] = 0xCC;

    TagPayload p1{tag1};
    TagPayload p2{tag2};

    if (ct::eq(p1, p2)) return 1;
    return 0;
}

int run_declassify_ct() {
    TestHmacTag tag{};
    for (size_t i = 0; i < 16; ++i) {
        tag.bytes[i] = static_cast<unsigned char>(0xDD);
    }

    TagPayload payload{tag};

    auto raw = std::move(payload).declassify_ct<secret_policy::HashForCompare>();
    if (raw.bytes[0] != 0xDD) return 1;
    if (raw.bytes[15] != 0xDD) return 2;
    return 0;
}

// A whole protocol in miniature: receive a claimed tag, compare it against
// the expected one, send the verdict.

using AuthVerifyProto = Recv<TagPayload, Send<bool, End>>;

struct MockChannel {
    TestHmacTag claimed_tag{};
    bool ack_sent = false;
    bool ack_value = false;
};

int run_worked_example_hmac_verify() {
    MockChannel channel;
    // The tag that arrives on the wire.
    for (size_t i = 0; i < 16; ++i) {
        channel.claimed_tag.bytes[i] = static_cast<unsigned char>(0x11);
    }

    auto handle = mint_session_handle<AuthVerifyProto>(&channel);

    auto [received_payload, after_recv] =
        std::move(handle).recv([](MockChannel*& c) noexcept -> TagPayload { return TagPayload{c->claimed_tag}; });

    // Here the expected tag matches the claimed one.
    TestHmacTag expected{};
    for (size_t i = 0; i < 16; ++i) {
        expected.bytes[i] = static_cast<unsigned char>(0x11);
    }
    TagPayload expected_payload{expected};

    // The constant-time comparison is the only one that compiles, since the
    // ordinary operators are deleted.
    bool ok = ct::eq(received_payload, expected_payload);
    if (!ok) return 1;

    auto end_handle = std::move(after_recv).send(ok, [](MockChannel*& c, bool v) noexcept {
        c->ack_sent = true;
        c->ack_value = v;
    });

    if (!channel.ack_sent) return 2;
    if (!channel.ack_value) return 3;

    auto* recovered = std::move(end_handle).close();
    if (recovered != &channel) return 4;
    return 0;
}

int run_worked_example_hmac_mismatch() {
    MockChannel channel;
    for (size_t i = 0; i < 16; ++i) {
        channel.claimed_tag.bytes[i] = static_cast<unsigned char>(0x22);
    }

    auto handle = mint_session_handle<AuthVerifyProto>(&channel);
    auto [received_payload, after_recv] =
        std::move(handle).recv([](MockChannel*& c) noexcept -> TagPayload { return TagPayload{c->claimed_tag}; });

    // Here the expected tag differs by one byte.
    TestHmacTag expected{};
    for (size_t i = 0; i < 16; ++i) {
        expected.bytes[i] = static_cast<unsigned char>(0x22);
    }
    expected.bytes[10] = 0xFF;
    TagPayload expected_payload{expected};

    bool ok = ct::eq(received_payload, expected_payload);
    if (ok) return 1;

    auto end_handle = std::move(after_recv).send(ok, [](MockChannel*& c, bool v) noexcept {
        c->ack_sent = true;
        c->ack_value = v;
    });

    if (!channel.ack_sent) return 2;
    // The acknowledgement carries the verdict, so it must be false here.
    if (channel.ack_value) return 3;

    auto* recovered = std::move(end_handle).close();
    if (recovered != &channel) return 4;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_construct_and_bytes(); rc != 0) return rc;
    if (int rc = run_in_place_construction(); rc != 0) return 100 + rc;
    if (int rc = run_ct_eq_identical(); rc != 0) return 200 + rc;
    if (int rc = run_ct_eq_differing(); rc != 0) return 300 + rc;
    if (int rc = run_ct_eq_differs_at_first_byte(); rc != 0) return 400 + rc;
    if (int rc = run_declassify_ct(); rc != 0) return 500 + rc;
    if (int rc = run_worked_example_hmac_verify(); rc != 0) return 600 + rc;
    if (int rc = run_worked_example_hmac_mismatch(); rc != 0) return 700 + rc;

    std::puts("session_ct: opt-in trait + CTPayload + ct::eq + HMAC-verify OK");
    return 0;
}
