// POSITIVE-ATTACK REGRESSION TEST.  This file MUST PASS TODAY and
// MUST FAIL TO COMPILE when fixy-M-29 lands (namespace-private or
// friend-constrained splits_into).
//
// ── fixy-CR-05 — Cross-org permission escalation via user-side ────
//                splits_into specialization
//
// Threat model: an attacker holds a legitimate
// Permission<FederatedPeer<OrgA>> — minted through the standard
// admittance flow (mint_federation_admittance succeeds for OrgA).
// `splits_into<Parent, L, R>` in
// `include/crucible/permissions/Permission.h` is a user-extensible
// trait by design; FederationPermission.h ships a defensive
// partial specialization pinning intra-org only, but C++ partial-
// specialization ranking gives a fully-specialized explicit
// specialization with three concrete tag types higher rank than a
// parameterized partial.  Once the attacker writes:
//
//   namespace crucible::safety {
//   template <> struct splits_into<
//       tag::FederatedPeer<OrgA>,
//       tag::FederatedPeer<OrgB>,
//       tag::FederatedPeer<OrgB>>
//       : std::true_type {};
//   }
//
// the malicious specialization wins matching, mint_permission_split
// succeeds for cross-org template arguments, and the attacker
// converts one legitimate OrgA permission into TWO OrgB permissions
// with no admittance check, no federation handshake, no MAC.
//
// What this fixture proves (two attack patterns, both succeed
// today):
//
//   Pattern A — binary split-and-escalate: the attacker specializes
//               splits_into<FederatedPeer<OrgA>, FederatedPeer<OrgB>,
//               FederatedPeer<OrgB>> as true_type, then calls
//               mint_permission_split to convert one OrgA permission
//               into two OrgB permissions.
//
//   Pattern B — N-ary split-and-escalate: the attacker specializes
//               splits_into_pack<FederatedPeer<OrgA>, FederatedPeer<OrgB>,
//               FederatedPeer<OrgC>, FederatedPeer<OrgD>> as true_type,
//               then calls mint_permission_split_n to fan one OrgA
//               permission into three distinct cross-org permissions.
//               The N-ary variant proves the gap is systemic, not
//               specific to the binary split.
//
// WHEN THIS TEST REDDENS (i.e., the build itself fails because the
// malicious user-side specializations no longer compile):
//   1. Confirm fixy-M-29 (or equivalent) has landed —
//      `splits_into` is now namespace-private, friend-constrained,
//      sealed via a CRTP base, or otherwise structurally non-
//      specializable from outside `crucible::safety`.
//   2. Rewrite THIS fixture as a NEGATIVE regression
//      (test/fixy_neg/neg_fixy_federation_cross_org_split.cpp): the
//      malicious specialization must produce a documented
//      diagnostic — `splits_into_specialization_not_permitted` or
//      similar — naming the relevant fixy-M-29 closure.
//   3. Remove the fixy-CR-05 section from the doc-block on
//      FederationPermission.h.
//
// Distinct from fixy-CR-02 / -03 / -04:
//   - CR-02: forge a handshake from public inputs (authentication
//     failure).
//   - CR-03: replay a captured handshake (freshness failure).
//   - CR-04: borrow a const-ref to LocalCipherPermission (authority-
//     proof failure).
//   - CR-05: convert a legitimate same-org Permission into a cross-
//     org Permission via user-side splits_into specialization
//     (admittance-policy bypass).
//
// All four together: forge handshake (CR-02 OR CR-03) × ANY const-ref
// to LocalCipherPermission (CR-04) = admit ANY org; then split that
// admittance into ANY org via user-side specialization (CR-05).  The
// V1 federation trust model is structurally not-yet-production-safe
// across four orthogonal axes.

#include <crucible/permissions/FederationPermission.h>

#include "../test_assert.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

// fixy-CR-02 — mint_federation_admittance is [[deprecated]] in V1.
// This attack regression knowingly exercises the placeholder
// verifier to obtain a legitimate same-org seed Permission before
// performing the CR-05 escalation.  Suppress the deprecation
// diagnostic at the TU level.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

// ── The malicious orgs.  Defined in this TU so the malicious
//    splits_into specializations below are well-formed.  Top-level
//    (NOT in anonymous namespace) so explicit splits_into
//    specializations can name them.  Empty struct identities are
//    sufficient for the federation_org_id consteval reflection.

struct AttackerOrgA {};
struct AttackerOrgB {};
struct AttackerOrgC {};
struct AttackerOrgD {};

// ── The malicious specializations.  These would belong in
//    crucible::safety::tag's own TU per the docstring's "specialize
//    next to your tags" discipline — fixy-CR-05 weaponizes the fact
//    that the discipline is review-only with zero structural
//    enforcement.  Any downstream user TU can ship these without
//    crucible::safety co-signing.

namespace crucible::safety {

// Pattern A — binary split: OrgA → OrgB × OrgB.
template <>
struct splits_into<
    ::crucible::permissions::tag::FederatedPeer<::AttackerOrgA>,
    ::crucible::permissions::tag::FederatedPeer<::AttackerOrgB>,
    ::crucible::permissions::tag::FederatedPeer<::AttackerOrgB>>
    : std::true_type {};

// Pattern B — N-ary split: OrgA → OrgB × OrgC × OrgD.
template <>
struct splits_into_pack<
    ::crucible::permissions::tag::FederatedPeer<::AttackerOrgA>,
    ::crucible::permissions::tag::FederatedPeer<::AttackerOrgB>,
    ::crucible::permissions::tag::FederatedPeer<::AttackerOrgC>,
    ::crucible::permissions::tag::FederatedPeer<::AttackerOrgD>>
    : std::true_type {};

}  // namespace crucible::safety

namespace {

// ── Confirm the malicious specializations are in scope ──────────
//
// The malicious specializations override the FederationPermission.h
// defensive partial because they are fully-specialized with three
// concrete types — strictly more specialized than the parameterized
// partial that ships with the federation tag tree.  These
// static_asserts are themselves the structural witness of the gap:
// today they pass, after fixy-M-29 lands the malicious
// specializations should fail to compile and the static_asserts
// reduce to "did not compile" — the whole TU reddens.

static_assert(saf::splits_into_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgB>>,
    "malicious binary splits_into specialization must win partial-"
    "specialization ranking over the FederationPermission.h "
    "defensive intra-org-only partial (today).");

static_assert(saf::splits_into_pack_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgC>,
    perm::tag::FederatedPeer<AttackerOrgD>>,
    "malicious N-ary splits_into_pack specialization must win "
    "ranking over the FederationPermission.h N-ary intra-org-only "
    "partial (today).");

// ── Helper: obtain a legitimate Permission<FederatedPeer<OrgA>> ─
//
// Eve goes through the standard admittance flow for OrgA — she IS
// legitimately admitted as OrgA (per a hypothetical admit_orgs
// policy that includes OrgA).  CR-05 escalates that legitimate
// admittance into cross-org admittance she has NO right to.
using AttackerPolicy = perm::policy::admit_orgs<
    AttackerOrgA, AttackerOrgB, AttackerOrgC, AttackerOrgD>;

perm::FederatedPeerPermission<AttackerOrgA>
obtain_legitimate_orga_permission() {
    auto local_cipher =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake = perm::make_self_signed_handshake<AttackerOrgA>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xC0FFEE'C0FFEEULL},
        /*nonce=*/      perm::Nonce{0xC1C1'C1C1'C1C1'C1C1ULL});

    auto admitted = perm::mint_federation_admittance<
        AttackerOrgA, AttackerPolicy>(local_cipher, handshake);
    assert(admitted.has_value());
    return std::move(*admitted);
}

// ── Pattern A — binary split-and-escalate ─────────────────────────
int attack_binary_cross_org_split() {
    // Step 1: Eve obtains her legitimate OrgA admittance.
    auto perm_a = obtain_legitimate_orga_permission();

    // Step 2: Eve invokes mint_permission_split with cross-org
    // template arguments.  Today this succeeds — the malicious
    // specialization at the top of this TU is `splits_into<
    // FederatedPeer<OrgA>, FederatedPeer<OrgB>, FederatedPeer<OrgB>>
    // : std::true_type`, which wins ranking over the defensive
    // intra-org-only partial in FederationPermission.h.
    auto [perm_b1, perm_b2] = saf::mint_permission_split<
        perm::tag::FederatedPeer<AttackerOrgB>,
        perm::tag::FederatedPeer<AttackerOrgB>>(std::move(perm_a));

    // Step 3: cross-org escalation observable — Eve now holds two
    // independent OrgB permissions despite never being admitted for
    // OrgB.  In a Linear-disciplined sound substrate this is
    // impossible: cross-org splits would either fail to compile
    // (fixy-M-29 closure) or require Eve to first prove OrgB
    // admittance via mint_federation_admittance<OrgB>.
    (void)perm_b1;
    (void)perm_b2;
    return 0;
}

// ── Pattern B — N-ary split-and-escalate ─────────────────────────
int attack_n_ary_cross_org_split() {
    auto perm_a = obtain_legitimate_orga_permission();

    // mint_permission_split_n with three distinct cross-org targets
    // — the malicious splits_into_pack specialization at the top of
    // this TU declares OrgA → OrgB × OrgC × OrgD as a valid split.
    auto children = saf::mint_permission_split_n<
        perm::tag::FederatedPeer<AttackerOrgB>,
        perm::tag::FederatedPeer<AttackerOrgC>,
        perm::tag::FederatedPeer<AttackerOrgD>>(std::move(perm_a));

    auto& perm_b = std::get<0>(children);
    auto& perm_c = std::get<1>(children);
    auto& perm_d = std::get<2>(children);
    (void)perm_b;
    (void)perm_c;
    (void)perm_d;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = attack_binary_cross_org_split(); rc != 0) {
        std::fprintf(stderr,
            "attack_binary_cross_org_split failed (rc=%d)\n", rc);
        return 1;
    }
    if (int rc = attack_n_ary_cross_org_split(); rc != 0) {
        std::fprintf(stderr,
            "attack_n_ary_cross_org_split failed (rc=%d)\n", rc);
        return 2;
    }

    std::puts(
        "attack_federation_cross_org_escalation: V1 splits_into "
        "trait is user-extensible; malicious explicit specializations "
        "win partial-specialization ranking over the federation tag "
        "tree's defensive intra-org-only partial, allowing one "
        "legitimate same-org Permission to be split into two-or-more "
        "cross-org Permissions with zero admittance check "
        "(fixy-CR-05).  When this TU fails to compile, fixy-M-29 has "
        "closed the structural gap — see the doc-block at the top of "
        "this file for the remediation checklist.");
    return 0;
}

#pragma GCC diagnostic pop
