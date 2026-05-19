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

// ── fixy-M-29 closure witness ───────────────────────────────────
//
// The malicious specializations above still win partial-specialization
// ranking over the FederationPermission.h defensive partial — the
// splits_into_v lookups below confirm Eve's specializations are in
// scope.  HOWEVER: after fixy-M-29 landed the type-level authoring
// witness (splits_into_authoring_witness_v), the mint_permission_split
// gate now demands BOTH splits_into AND its authoring witness.  Eve
// can specialize splits_into from a foreign TU but cannot specialize
// splits_into_authoring_witness for her malicious triple — the CI
// orphan-purity script (scripts/check-splits-into-orphan.sh) would
// reject the witness specialization at build time.  The
// well_authored_split_v predicate below witnesses the closure.
//
// This file changed mode at fixy-M-29: pre-M-29 it ran the attack
// and demonstrated cross-org escalation; post-M-29 it static_asserts
// that the witness gate rejects every malicious triple, while the
// splits_into_v predicates still report true (the gap is real, the
// type-system simply gates the mint surface above splits_into_v).

static_assert(saf::splits_into_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgB>>,
    "malicious binary splits_into specialization wins partial-"
    "specialization ranking over the FederationPermission.h "
    "defensive intra-org-only partial — this remains true; "
    "fixy-M-29 closes the gap at the mint gate, not the trait.");

static_assert(!saf::splits_into_authoring_witness_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgB>>,
    "fixy-M-29 closure: malicious binary splits_into specialization "
    "has NO accompanying splits_into_authoring_witness — the type "
    "system's defense-in-depth layer above splits_into_v.");

static_assert(!saf::well_authored_split_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgB>>,
    "fixy-M-29 closure: well_authored_split_v rejects Eve's cross-"
    "org binary split — mint_permission_split would static_assert.");

static_assert(saf::splits_into_pack_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgC>,
    perm::tag::FederatedPeer<AttackerOrgD>>,
    "malicious N-ary splits_into_pack specialization must win "
    "ranking over the FederationPermission.h N-ary intra-org-only "
    "partial — still true; fixy-M-29 closes the gap at the mint gate.");

static_assert(!saf::splits_into_pack_authoring_witness_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgC>,
    perm::tag::FederatedPeer<AttackerOrgD>>,
    "fixy-M-29 closure: malicious N-ary splits_into_pack has NO "
    "accompanying splits_into_pack_authoring_witness.");

static_assert(!saf::well_authored_split_pack_v<
    perm::tag::FederatedPeer<AttackerOrgA>,
    perm::tag::FederatedPeer<AttackerOrgB>,
    perm::tag::FederatedPeer<AttackerOrgC>,
    perm::tag::FederatedPeer<AttackerOrgD>>,
    "fixy-M-29 closure: well_authored_split_pack_v rejects Eve's "
    "cross-org N-ary split — mint_permission_split_n would "
    "static_assert.");

// ── Helper: obtain a legitimate Permission<FederatedPeer<OrgA>> ─
//
// Eve goes through the standard admittance flow for OrgA — she IS
// legitimately admitted as OrgA (per a hypothetical admit_orgs
// policy that includes OrgA).  CR-05 escalates that legitimate
// admittance into cross-org admittance she has NO right to.
using AttackerPolicy = perm::policy::admit_orgs<
    AttackerOrgA, AttackerOrgB, AttackerOrgC, AttackerOrgD>;

[[maybe_unused]] perm::FederatedPeerPermission<AttackerOrgA>
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

// ── Pattern A — binary split-and-escalate (NEUTRALIZED post-M-29) ─
//
// Pre-fixy-M-29 this function actually invoked
// `saf::mint_permission_split<FederatedPeer<OrgB>, FederatedPeer<OrgB>>`
// on Eve's legitimate OrgA permission and escalated to two OrgB
// permissions.  fixy-M-29 closed the gap at the mint gate via the
// well_authored_split_v concept: the static_asserts above prove the
// gate rejects.  The call sites are commented out — uncommenting any
// of them is a compile error today, which IS the closure witness.
int attack_binary_cross_org_split() {
    // Pre-M-29 attack (would fire the well_authored_split_v
    // static_assert today — kept as documentation):
    //   auto perm_a = obtain_legitimate_orga_permission();
    //   auto [perm_b1, perm_b2] = saf::mint_permission_split<
    //       perm::tag::FederatedPeer<AttackerOrgB>,
    //       perm::tag::FederatedPeer<AttackerOrgB>>(std::move(perm_a));
    return 0;
}

// ── Pattern B — N-ary split-and-escalate (NEUTRALIZED post-M-29) ──
int attack_n_ary_cross_org_split() {
    // Pre-M-29 attack (would fire the well_authored_split_pack_v
    // static_assert today — kept as documentation):
    //   auto perm_a = obtain_legitimate_orga_permission();
    //   auto children = saf::mint_permission_split_n<
    //       perm::tag::FederatedPeer<AttackerOrgB>,
    //       perm::tag::FederatedPeer<AttackerOrgC>,
    //       perm::tag::FederatedPeer<AttackerOrgD>>(std::move(perm_a));
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
