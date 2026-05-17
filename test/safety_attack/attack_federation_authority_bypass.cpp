// POSITIVE-ATTACK REGRESSION TEST.  This file MUST PASS TODAY and
// MUST RED when value-based local authority lands.
//
// ── fixy-CR-04 — Local cipher proof is type-only, value-ignored ────
//
// Threat model: an attacker (Eve) shares the process with Module A,
// the legitimate cipher owner.  Eve cannot mint
// Permission<LocalCipherTag> herself (suppose CR-06 has been
// independently closed and `mint_permission_root<LocalCipherTag>()`
// is only callable from `main()`'s init path).  But Eve CAN obtain
// a `const LocalCipherPermission&` ref to Module A's permission
// through any of several plausible channels:
//
//   1. Module A exposes a singleton accessor (the canonical pattern
//      shown in `test_federation_permission.cpp` and
//      `attack_federation_forgery.cpp`).  Eve calls the accessor.
//   2. Module A passes its ref into a cipher-internal helper Eve
//      has compromised (sandbox escape, plugin host, deep callee).
//      Eve's helper repurposes the ref for federation admittance.
//   3. Module A has multiple legitimate caller sites; one of them
//      stores the ref in a cache that Eve can read.
//
// The substrate's mint signature is:
//
//   mint_federation_admittance<Org, Policy>(
//       const LocalCipherPermission& local_permission,
//       FederationHandshake handshake) noexcept
//
// And the body discards the proof:
//
//   { (void)local_permission; ... }
//
// The verifier trusts the TYPE — `Permission<tag::LocalCipherTag>`
// IS the authority claim — but never inspects the bytes.  Any
// borrowed ref produces the same admittance as a freshly-minted
// one.
//
// What this fixture proves (three attack patterns, all succeed
// today):
//
//   Pattern A — singleton-leak: Eve calls Module A's singleton
//               accessor to obtain a const-ref, forges a handshake,
//               and mints federation admittance.  Eve never called
//               mint_permission_root herself.
//
//   Pattern B — aliased-N-callers: the same const-ref is borrowed
//               by N independent attacker call sites, each minting
//               a distinct admittance.  The Linear discipline of
//               `Permission<Tag>` (move-only) does NOT prevent
//               aliasing through `const&`.
//
//   Pattern C — trust-transitivity: Module A passes its ref into a
//               deep callee for a totally different purpose
//               (cipher encryption, say).  The callee, having
//               received the ref, repurposes it for federation
//               admittance.  Privilege escalation through
//               structural ref-passing.
//
// WHEN THIS TEST REDDENS (i.e., an `assert(admitted.has_value())`
// fires because mint_federation_admittance now returns
// `AdmittanceError::UnauthorizedLocalCipher` or similar value-
// based-check failure):
//   1. Confirm value-based local-cipher authority has landed
//      (HACL*-backed MAC, per-cipher secret in
//      Permission<LocalCipherTag>'s storage, etc.).
//   2. Update mint_federation_admittance to advertise the new
//      `AdmittanceError::UnauthorizedLocalCipher` enumerator.
//   3. Remove the fixy-CR-04 line from the `[[deprecated]]`
//      message on mint_federation_admittance (and the doc-block
//      section).
//   4. Rewrite THIS fixture as a NEGATIVE regression: prove that
//      a borrowed ref pointing to the wrong cipher's material is
//      rejected with the new error enumerator.
//
// Distinct from `attack_federation_forgery.cpp` (CR-02) and
// `attack_federation_replay.cpp` (CR-03):
//   - CR-02: forge a handshake from public inputs.
//   - CR-03: capture & replay a legitimate handshake.
//   - CR-04: ANY borrower of the local permission ref can mint,
//            with EITHER a forged OR a legitimate handshake.

#include <crucible/permissions/FederationPermission.h>

#include "../test_assert.h"

// fixy-CR-02 — mint_federation_admittance is [[deprecated]] in V1.
// This attack regression knowingly exercises the placeholder
// verifier to lock the local-authority-bypass vulnerability into
// CI.  Suppress the deprecation diagnostic at the TU level.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <cstdint>
#include <cstdio>

namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

// Anonymous namespace wrapper gives internal linkage to all
// helpers below, satisfying -Wmissing-declarations.  The "Module A"
// and "Module B" namespaces nested inside are scoping conveniences
// to model the threat model, not separate TUs.
namespace {

// ─── Module A — the legitimate, authorized cipher owner ────────────
//
// Sets up the LocalCipherPermission via the canonical
// mint_permission_root flow and exposes it through a
// static-singleton accessor.  This mirrors how real Crucible code
// paths obtain the permission (once-per-program init, cached
// reference), as seen in `test_federation_permission.cpp:47-51` and
// `attack_federation_forgery.cpp:99-103`.
namespace module_a {

const perm::LocalCipherPermission& exposed_local_cipher() {
    static const auto permission =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    return permission;
}

}  // namespace module_a

// ─── Module B — the attacker ───────────────────────────────────────
//
// Imports the substrate header (which is public anyway) and
// obtains the const-ref through one of three channels per attack
// pattern below.  Module B did NOT call mint_permission_root
// itself.
namespace module_b_attacker {

struct EveOrgTarget {};  // the org Eve wants to admit tokens for
using EvesPolicy = perm::policy::admit_orgs<EveOrgTarget>;

// ── Pattern A — singleton-leak ────────────────────────────────────
int attack_mint_via_borrowed_authority() {
    // Step 1: borrow the local cipher permission ref from Module
    // A.  No call to mint_permission_root here — Eve is NOT
    // authorized to mint root permissions; she only borrows.
    const perm::LocalCipherPermission& stolen_proof =
        module_a::exposed_local_cipher();

    // Step 2: forge a handshake from public inputs (fixy-CR-02
    // composition).  org_id is reachable via consteval
    // federation_org_id<Org> from any TU.
    auto forged_hs = perm::make_self_signed_handshake<EveOrgTarget>(
        /*peer_key_fp=*/0xEAEAEAEAEAEAEAEAULL,
        /*nonce=*/      0xEBEBEBEBEBEBEBEBULL);

    // Step 3: mint federation admittance using the BORROWED ref.
    // The substrate (void)-discards local_permission so it never
    // detects that Eve is borrowing rather than owning.  The
    // verifier has no signal distinguishing "legitimate caller
    // with the real proof" from "Eve with a borrowed ref to the
    // real proof".
    auto admitted = perm::mint_federation_admittance<
        EveOrgTarget, EvesPolicy>(stolen_proof, forged_hs);
    assert(admitted.has_value());
    return 0;
}

// ── Pattern B — aliased-N-callers ─────────────────────────────────
int attack_multiple_aliased_borrows() {
    // The same ref is borrowed by N independent attacker call
    // sites, each minting their own admittance.  In a Linear-
    // disciplined world the Permission<Tag> is move-only and
    // uniquely owned; only ONE caller can hold it at a time.
    // Here, N callers each "hold" a const& to the SAME backing
    // object and each succeeds — the Linear discipline of the
    // wrapped type does NOT propagate through `const&` aliasing.
    const perm::LocalCipherPermission& shared_ref_1 =
        module_a::exposed_local_cipher();
    const perm::LocalCipherPermission& shared_ref_2 =
        module_a::exposed_local_cipher();  // same backing object
    const perm::LocalCipherPermission& shared_ref_3 =
        module_a::exposed_local_cipher();  // same backing object

    // All three refs point to the same `static const auto` inside
    // Module A.  The substrate cannot tell them apart.
    auto hs1 = perm::make_self_signed_handshake<EveOrgTarget>(
        0xA1A1A1A1A1A1A1A1ULL, 0x1111111111111111ULL);
    auto hs2 = perm::make_self_signed_handshake<EveOrgTarget>(
        0xB2B2B2B2B2B2B2B2ULL, 0x2222222222222222ULL);
    auto hs3 = perm::make_self_signed_handshake<EveOrgTarget>(
        0xC3C3C3C3C3C3C3C3ULL, 0x3333333333333333ULL);

    auto admit_1 = perm::mint_federation_admittance<
        EveOrgTarget, EvesPolicy>(shared_ref_1, hs1);
    auto admit_2 = perm::mint_federation_admittance<
        EveOrgTarget, EvesPolicy>(shared_ref_2, hs2);
    auto admit_3 = perm::mint_federation_admittance<
        EveOrgTarget, EvesPolicy>(shared_ref_3, hs3);

    assert(admit_1.has_value());
    assert(admit_2.has_value());
    assert(admit_3.has_value());
    return 0;
}

// ── Pattern C — deep-callee trust-transitivity ────────────────────

// A deep callee that Module A trusts for a totally different
// purpose — e.g., it promises to do cipher encryption only.  But
// once it receives the const-ref, the substrate gives it the same
// federation-admittance authority as the original caller.
// Privilege escalation through structural ref-passing.
int deep_callee_abuses_received_ref(
    const perm::LocalCipherPermission& trusted_ref) {
    // The deep callee got the ref ostensibly to encrypt a payload.
    // Instead it mints federation admittance, exploiting the fact
    // that the substrate's mint doesn't distinguish "ref passed
    // for cipher encryption" from "ref passed for federation
    // admittance".
    auto repurposed_hs = perm::make_self_signed_handshake<EveOrgTarget>(
        0xABBAABBAABBAABBAULL, 0xACDCACDCACDCACDCULL);
    auto admitted = perm::mint_federation_admittance<
        EveOrgTarget, EvesPolicy>(trusted_ref, repurposed_hs);
    assert(admitted.has_value());
    return 0;
}

int attack_trust_transitivity_through_ref_passing() {
    // Module A passes its ref to a deep callee.  In Module A's
    // mental model, the callee only does cipher encryption — but
    // structurally, the callee inherits the full set of
    // operations the ref-type can authorize.  The substrate's
    // `const LocalCipherPermission&` parameter at
    // mint_federation_admittance accepts the inherited ref
    // without distinguishing the original-caller's intent from
    // the callee's actual use.
    return deep_callee_abuses_received_ref(
        module_a::exposed_local_cipher());
}

}  // namespace module_b_attacker

}  // anonymous namespace

int main() {
    if (int rc = module_b_attacker::attack_mint_via_borrowed_authority();
        rc != 0) {
        std::fprintf(stderr,
            "attack_mint_via_borrowed_authority failed (rc=%d)\n", rc);
        return 1;
    }
    if (int rc = module_b_attacker::attack_multiple_aliased_borrows();
        rc != 0) {
        std::fprintf(stderr,
            "attack_multiple_aliased_borrows failed (rc=%d)\n", rc);
        return 2;
    }
    if (int rc =
            module_b_attacker::attack_trust_transitivity_through_ref_passing();
        rc != 0) {
        std::fprintf(stderr,
            "attack_trust_transitivity_through_ref_passing failed (rc=%d)\n",
            rc);
        return 3;
    }

    std::puts(
        "attack_federation_authority_bypass: V1 mint trusts ANY "
        "const-ref to a LocalCipherPermission, including borrowed, "
        "aliased, and trust-transitively-received refs "
        "(fixy-CR-04).  When this test REDDENS, value-based local "
        "authority has landed — see the doc-block at the top of "
        "this file for the remediation checklist.");
    return 0;
}

#pragma GCC diagnostic pop
