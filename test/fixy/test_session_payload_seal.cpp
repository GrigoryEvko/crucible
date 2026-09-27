// The payload verdicts are sealed against a user specialization.
//
// fixy/session/Payload.h computes every verdict in one walk that is not a
// template, and it caches the result in an alias template, so no
// specialization changes what a verdict reads.  Each predicate is a
// concept, and a specialization of a concept does not compile (the
// neg_sess_seal_* fixtures).  The class templates of the same names are
// views for a reader.  This unit specializes each view, the carrier view,
// each gate and the cell of each sealed cache, for payloads that the walk
// refuses.  Each verdict of the session layer still refuses them.

#include <fixy/session/Checkpoint.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Delegate.h>

#include <cstdio>
#include <type_traits>

namespace test_session_payload_seal_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct Alice {};
struct Bob {};

// A payload that holds a live endpoint by value.  The walk refuses it.
using Endpoint = s::SessionHandle<s::Recv<int, s::End>, Wire, void, s::check::Enforced, fp::PermSet<Region>>;
struct Evil {
    int sequence = 0;
    Endpoint endpoint;
};

// A hand-off, which delegates.  A crash session and a checkpoint session
// refuse it.
using Handed = s::DelegatedSession<s::End, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

// A set with a region lent out.
using Lent = fp::PermSet<s::LentOut<Region>>;

using SendsEvil = s::Send<Evil, s::End>;

}  // namespace test_session_payload_seal_types

namespace seal = test_session_payload_seal_types;

// ── The specializations ─────────────────────────────────────────────

template <>
struct fixy::session::is_permission_classified<seal::Evil> : std::true_type {};

template <>
struct fixy::session::is_plain_payload<seal::Evil> : std::true_type {};

template <>
struct fixy::session::payload_perm_delta<seal::Evil> {
    using sender_requires = ::foundation::permissions::EmptyPermSet;
    using sender_loses = sender_requires;
    using sender_gains = ::foundation::permissions::EmptyPermSet;
    using receiver_requires = ::foundation::permissions::EmptyPermSet;
    using receiver_loses = receiver_requires;
    using receiver_gains = ::foundation::permissions::EmptyPermSet;
    static constexpr bool carries_share = false;
};

template <>
struct fixy::session::protocol_delivered_regions<seal::SendsEvil> {
    using type = ::foundation::permissions::EmptyPermSet;
};

template <>
inline constexpr fixy::session::DelegationCarrier fixy::session::payload_delegation_carrier_v<seal::Handed> =
    fixy::session::DelegationCarrier::None;

template <>
struct fixy::session::detail::payload_conveys_delegation<seal::Handed> : std::false_type {};

// A gate without its text.  holds stays true, so the gate refuses nothing
// more, and the verdict comes from the facts.
template <>
struct fixy::session::detail::payload_readable_gate<seal::Evil> {
    static constexpr bool holds = true;
};

template <>
struct fixy::session::detail::payload_admitted_gate<seal::Evil> {
    static constexpr bool holds = true;
};

template <>
struct fixy::session::detail::delegation_readable_gate<seal::Handed> {
    static constexpr bool holds = true;
};

template <>
struct fixy::session::detail::delivery_gate<seal::SendsEvil> {
    static constexpr bool holds = true;
};

// A body for the cell of the cached facts.  The reader takes the facts
// from the value argument of the cell, so the body changes nothing.
template <>
struct fixy::session::detail::payload_facts_cell<fixy::session::detail::payload_facts(^^seal::Evil)> {
    static constexpr bool is_admitted = true;
};

namespace test_session_payload_seal_types {

// ── The specializations took ────────────────────────────────────────
//
// A reader of a view sees what the specialization states.

static_assert(s::is_permission_classified<Evil>::value && s::is_plain_payload<Evil>::value);
static_assert(std::is_same_v<s::payload_perm_delta<Evil>::sender_requires, fp::EmptyPermSet>);
static_assert(s::payload_delegation_carrier_v<Handed> == s::DelegationCarrier::None);
static_assert(!s::detail::payload_conveys_delegation<Handed>::value);

// ── Each verdict still refuses ──────────────────────────────────────

static_assert(!s::is_permission_classified_v<Evil> && !s::is_plain_payload_v<Evil>);
static_assert(!s::SendablePayload<Evil, fp::EmptyPermSet> && !s::ReceivablePayload<Evil, fp::EmptyPermSet>);
static_assert(!s::PermissionFlowCloses<SendsEvil, fp::EmptyPermSet>);
static_assert(!s::PermissionFlowCloses<s::Recv<Evil, s::End>, fp::EmptyPermSet>);

static_assert(s::payload_conveys_delegation_v<Handed>);
static_assert(!s::CrashSessionAdmissible<s::Offer<s::Recv<Handed, s::End>, s::Recv<s::Crash<Bob>, s::End>>, Alice, Bob,
                                         s::NoReliableRoles>);
using DelegatesInCheckpoint = s::Select<s::Commit<s::Send<Handed, s::End>>, s::Roll>;
static_assert(s::checkpoint_verdict_v<DelegatesInCheckpoint, s::dual_of_t<DelegatesInCheckpoint>>
              == s::CheckpointVerdict::NotCheckpointShaped);

static_assert(s::perm_set_has_open_loan_v<Lent> && !s::PermissionFlowCloses<s::End, Lent>);

}  // namespace test_session_payload_seal_types

int main() {
    std::puts("test_session_payload_seal: every verdict reads the facts of the walk");
    return 0;
}
