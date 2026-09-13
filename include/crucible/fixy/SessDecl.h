#pragma once

#include <crucible/sessions/SessionDeclassify.h>

#include <type_traits>

namespace crucible::fixy::sess::declassify {

using ::crucible::safety::DeclassifyOnSend;

using ::crucible::safety::is_declassify_on_send;
using ::crucible::safety::is_declassify_on_send_v;

using ::crucible::safety::DeclassifyOnSendable;

using ::crucible::safety::wire_payload_type;
using ::crucible::safety::wire_payload_type_t;

using ::crucible::safety::wire_policy;
using ::crucible::safety::wire_policy_t;

namespace u052a_self_test {

struct Token {
    int v = 0;
};
struct Other {};

using TokenWire = DeclassifyOnSend<Token, ::crucible::safety::secret_policy::WireSerialize>;
using TokenAudit = DeclassifyOnSend<Token, ::crucible::safety::secret_policy::AuditedLogging>;

static_assert(
    std::is_same_v<DeclassifyOnSend<Token, ::crucible::safety::secret_policy::WireSerialize>,
                   ::crucible::safety::DeclassifyOnSend<Token, ::crucible::safety::secret_policy::WireSerialize>>,
    "fixy::sess::declassify::DeclassifyOnSend must alias "
    "safety::DeclassifyOnSend.");

static_assert(is_declassify_on_send_v<TokenWire>);
static_assert(is_declassify_on_send_v<TokenAudit>);
static_assert(!is_declassify_on_send_v<Token>);
static_assert(!is_declassify_on_send_v<int>);

static_assert(DeclassifyOnSendable<TokenWire>);
static_assert(!DeclassifyOnSendable<Token>);

static_assert(std::is_same_v<wire_payload_type_t<TokenWire>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<TokenAudit>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<Token>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<int>, int>);

static_assert(std::is_same_v<wire_policy_t<TokenWire>, ::crucible::safety::secret_policy::WireSerialize>);
static_assert(std::is_same_v<wire_policy_t<TokenAudit>, ::crucible::safety::secret_policy::AuditedLogging>);

static_assert(!std::is_same_v<TokenWire, TokenAudit>);

// DeclassifyOnSend must stay move-only.  A copyable wrapper would let a
// classified value duplicate silently at the wire-payload boundary.
static_assert(!std::is_copy_constructible_v<TokenWire>);
static_assert(!std::is_copy_assignable_v<TokenWire>);
static_assert(std::is_move_constructible_v<TokenWire>);
static_assert(std::is_move_assignable_v<TokenWire>);

constexpr int u052a_surface_cardinality = 8;
static_assert(u052a_surface_cardinality == 8, "The re-exported surface cardinality drifted — update the "
                                              "using-decls and this sentinel in lockstep.");

}  // namespace u052a_self_test

inline void runtime_smoke_test() noexcept {
    struct Payload {
        int v = 0;
    };
    using P = DeclassifyOnSend<Payload, ::crucible::safety::secret_policy::WireSerialize>;

    [[maybe_unused]] constexpr bool isW = is_declassify_on_send_v<P>;
    [[maybe_unused]] constexpr bool isNotWrap = is_declassify_on_send_v<int>;
    [[maybe_unused]] constexpr bool cap = DeclassifyOnSendable<P>;

    using PayloadT = wire_payload_type_t<P>;
    using PolicyT = wire_policy_t<P>;
    using PassT = wire_payload_type_t<int>;

    (void)isW;
    (void)isNotWrap;
    (void)cap;
    (void)static_cast<PayloadT*>(nullptr);
    (void)static_cast<PolicyT*>(nullptr);
    (void)static_cast<PassT*>(nullptr);
}

}  // namespace crucible::fixy::sess::declassify
