#pragma once

#include <crucible/safety/_NumericalTier.h>
// Ships is_subsort specialisations and no names. Without it the primary
// template answers false for every payload pair.
#include <crucible/sessions/SessionPayloadSubsort.h>
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/SessionSubtypeReason.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::subtype {

using ::crucible::safety::proto::is_subsort;
using ::crucible::safety::proto::is_subtype_sync_structural;
using ::crucible::safety::proto::is_subtype_sync;

using ::crucible::safety::proto::is_subsort_v;
using ::crucible::safety::proto::protocol_grade_satisfies_v;
using ::crucible::safety::proto::is_subtype_sync_v;
using ::crucible::safety::proto::equivalent_sync_v;
using ::crucible::safety::proto::is_strict_subtype_sync_v;
using ::crucible::safety::proto::subtype_chain_v;

using ::crucible::safety::proto::SubtypeSync;
using ::crucible::safety::proto::EquivalentSync;
using ::crucible::safety::proto::StrictSubtypeSync;
using ::crucible::safety::proto::CompatibleClient;
using ::crucible::safety::proto::CompatibleServer;

using ::crucible::safety::proto::assert_subtype_sync;
using ::crucible::safety::proto::assert_vendor_subtype_sync;
using ::crucible::safety::proto::check_protocol_evolution;
using ::crucible::safety::proto::assert_equivalent_sync;
using ::crucible::safety::proto::assert_compatible_client;
using ::crucible::safety::proto::assert_compatible_server;

using ::crucible::safety::proto::SubtypeOk;
using ::crucible::safety::proto::RejectionReason;

using ::crucible::safety::proto::is_rejection_reason;
using ::crucible::safety::proto::is_rejection_reason_v;
using ::crucible::safety::proto::is_subtype_sync_diag_v;

using ::crucible::safety::proto::subtype_rejection_reason;
using ::crucible::safety::proto::subtype_rejection_reason_t;

using ::crucible::safety::proto::assert_subtype_sync_diag;
using ::crucible::safety::proto::subtype_diag_agrees_v;

}  // namespace crucible::fixy::sess::subtype

namespace crucible::fixy::sess::subtype::u052e_self_test {

namespace proto = ::crucible::safety::proto;

using End = proto::End;
using SInt = proto::Send<int, proto::End>;
using RInt = proto::Recv<int, proto::End>;

static_assert(std::is_same_v<is_subsort<int, int>, proto::is_subsort<int, int>>);
static_assert(std::is_same_v<is_subtype_sync_structural<End, End>, proto::is_subtype_sync_structural<End, End>>);
static_assert(std::is_same_v<is_subtype_sync<End, End>, proto::is_subtype_sync<End, End>>);
static_assert(std::is_same_v<RejectionReason<proto::diagnostic::SubtypeMismatch, int, int>,
                             proto::RejectionReason<proto::diagnostic::SubtypeMismatch, int, int>>);

static_assert(is_subsort_v<int, int>, "subsort is reflexive");
static_assert(!is_subsort_v<int, double>, "default subsort is invariant (is_same)");

static_assert(is_subtype_sync_v<End, End>, "End is a subtype of itself");
static_assert(!is_subtype_sync_v<SInt, RInt>, "Send and Recv are incomparable shapes — the canonical "
                                              "send/recv-confusion bug must be rejected.");
static_assert(protocol_grade_satisfies_v<End, End>, "End trivially satisfies End's (empty) product-lattice grade.");

static_assert(equivalent_sync_v<End, End>, "End ≡ End (bidirectional)");
static_assert(!is_strict_subtype_sync_v<End, End>, "reflexive pair is NOT a strict subtype (strict is irreflexive)");
static_assert(subtype_chain_v<End, End, End>, "End ⩽ End ⩽ End");

static_assert(SubtypeSync<End, End>);
static_assert(EquivalentSync<End, End>);
// Compatibility holds when one protocol is a subtype of the other's dual,
// so a Send client pairs with a Recv server.
static_assert(CompatibleClient<SInt, RInt>);
static_assert(CompatibleServer<RInt, SInt>);

static_assert(std::is_same_v<subtype_rejection_reason_t<End, End>, SubtypeOk>,
              "End ⩽ End yields the SubtypeOk success sentinel.");
static_assert(is_subtype_sync_diag_v<End, End>, "diag path agrees with the bool path on the positive case.");
static_assert(subtype_diag_agrees_v<End, End>, "is_subtype_sync_diag_v and is_subtype_sync_v must agree.");
static_assert(is_rejection_reason_v<subtype_rejection_reason_t<SInt, RInt>>,
              "a shape mismatch produces a RejectionReason record, not SubtypeOk.");
static_assert(!is_rejection_reason_v<SubtypeOk>, "the success sentinel is NOT a rejection record.");

static_assert(!std::is_same_v<is_subtype_sync<End, End>, is_subtype_sync_structural<End, End>>,
              "grade-filtered and structural relations are distinct templates.");

constexpr int u052e_surface_cardinality = 29;
static_assert(u052e_surface_cardinality == 29, "fixy::sess::subtype:: surface cardinality drifted — "
                                               "update the using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::subtype::u052e_self_test

namespace crucible::fixy::sess::subtype::v067_payload_axiom_test {

namespace saf = ::crucible::safety;
namespace prot = ::crucible::safety::proto;
namespace fsub = ::crucible::fixy::sess::subtype;

struct Payload {
    int v;
};

static_assert(fsub::is_subsort_v<saf::Refined<saf::positive, int>, int>);
static_assert(fsub::is_subsort_v<saf::Refined<saf::non_negative, int>, int>);
static_assert(fsub::is_subsort_v<saf::Refined<saf::non_zero, int>, int>);
static_assert(!fsub::is_subsort_v<int, saf::Refined<saf::positive, int>>,
              "a bare T has no proof — reverse narrowing must be rejected.");

static_assert(fsub::is_subsort_v<saf::Refined<saf::positive, int>, saf::Refined<saf::non_negative, int>>,
              "positive ⇒ non_negative, so the stronger refinement flows.");
static_assert(!fsub::is_subsort_v<saf::Refined<saf::non_negative, int>, saf::Refined<saf::positive, int>>,
              "the weaker refinement does NOT flow to the stronger position.");

static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::Sanitized>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromInternal>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromConfig>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromDb>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::Durable>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::Computed>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::vessel_trust::Validated>, Payload>);

static_assert(!fsub::is_subsort_v<saf::Tagged<Payload, saf::source::External>, Payload>,
              "External provenance must be validated before flowing to bare T.");
static_assert(!fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromUser>, Payload>);
static_assert(!fsub::is_subsort_v<saf::Tagged<Payload, saf::vessel_trust::FromPytorch>, Payload>);

// Epistemic, access and version tags carry content of their own, so they
// do not erase into the bare payload.
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::trust::Verified>, int>);
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::trust::Unverified>, int>);
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::access::RO>, int>);
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::version::V<1>>, int>);

static_assert(!fsub::is_subsort_v<int, saf::Tagged<int, saf::source::Sanitized>>);

static_assert(fsub::is_subsort_v<saf::Refined<saf::positive, saf::Tagged<int, saf::source::Sanitized>>,
                                 saf::Tagged<int, saf::source::Sanitized>>);

using BitexactT = saf::NumericalTier<saf::Tolerance::BITEXACT, Payload>;
using RelaxedT = saf::NumericalTier<saf::Tolerance::RELAXED, Payload>;
static_assert(fsub::is_subsort_v<BitexactT, RelaxedT>, "a bit-exact producer guarantee satisfies a relaxed consumer.");
static_assert(!fsub::is_subsort_v<RelaxedT, BitexactT>);

static_assert(
    fsub::is_subtype_sync_v<prot::Send<saf::Refined<saf::positive, int>, prot::End>, prot::Send<int, prot::End>>,
    "Send is payload-covariant: refined payload flows to bare-T position.");
static_assert(
    !fsub::is_subtype_sync_v<prot::Send<int, prot::End>, prot::Send<saf::Refined<saf::positive, int>, prot::End>>,
    "the reverse (bare T into a refined Send position) is rejected.");
static_assert(
    fsub::is_subtype_sync_v<prot::Recv<int, prot::End>, prot::Recv<saf::Refined<saf::positive, int>, prot::End>>,
    "Recv is payload-contravariant — the directions flip.");

constexpr int v067_positive_axiom_families = 6;
static_assert(v067_positive_axiom_families == 6, "fixy::sess::subtype:: positive-axiom-family count drifted — "
                                                 "update the payload-subsort witnesses AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::subtype::v067_payload_axiom_test

namespace crucible::fixy::sess::subtype {

// Static assertions alone leave the consteval helpers uninstantiated, so a
// SFINAE or inline-body fault in one of them stays hidden. Calling each from
// a real body forces the instantiation. Every relation below holds, so the
// assertions inside the helpers pass.

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    using End = proto::End;
    using SInt = proto::Send<int, proto::End>;
    using RInt = proto::Recv<int, proto::End>;

    assert_subtype_sync<End, End>();
    assert_vendor_subtype_sync<End, End>();
    check_protocol_evolution<End, End>();
    assert_equivalent_sync<End, End>();
    assert_compatible_client<SInt, RInt>();
    assert_compatible_server<RInt, SInt>();
    assert_subtype_sync_diag<End, End>();

    [[maybe_unused]] constexpr bool sub = is_subtype_sync_v<End, End>;
    [[maybe_unused]] constexpr bool not_sub = is_subtype_sync_v<SInt, RInt>;
    [[maybe_unused]] constexpr bool equiv = equivalent_sync_v<End, End>;
    [[maybe_unused]] constexpr bool strict = is_strict_subtype_sync_v<End, End>;
    [[maybe_unused]] constexpr bool chain = subtype_chain_v<End, End, End>;
    [[maybe_unused]] constexpr bool diag = is_subtype_sync_diag_v<End, End>;
    [[maybe_unused]] constexpr bool agrees = subtype_diag_agrees_v<End, End>;

    using OkReason = subtype_rejection_reason_t<End, End>;
    using FailReason = subtype_rejection_reason_t<SInt, RInt>;
    [[maybe_unused]] constexpr bool ok_is_ok = std::is_same_v<OkReason, SubtypeOk>;
    [[maybe_unused]] constexpr bool fail_is_rej = is_rejection_reason_v<FailReason>;

    (void)sub;
    (void)not_sub;
    (void)equiv;
    (void)strict;
    (void)chain;
    (void)diag;
    (void)agrees;
    (void)ok_is_ok;
    (void)fail_is_rej;

    namespace saf = ::crucible::safety;
    [[maybe_unused]] constexpr bool refined_narrows = is_subsort_v<saf::Refined<saf::positive, int>, int>;
    [[maybe_unused]] constexpr bool refined_strengthens =
        is_subsort_v<saf::Refined<saf::positive, int>, saf::Refined<saf::non_negative, int>>;
    [[maybe_unused]] constexpr bool tag_safe_erases = is_subsort_v<saf::Tagged<int, saf::source::Sanitized>, int>;
    [[maybe_unused]] constexpr bool tag_unsafe_blocked = !is_subsort_v<saf::Tagged<int, saf::source::External>, int>;
    [[maybe_unused]] constexpr bool send_payload_covariant =
        is_subtype_sync_v<proto::Send<saf::Refined<saf::positive, int>, proto::End>, proto::Send<int, proto::End>>;

    (void)refined_narrows;
    (void)refined_strengthens;
    (void)tag_safe_erases;
    (void)tag_unsafe_blocked;
    (void)send_payload_covariant;
}

}  // namespace crucible::fixy::sess::subtype
