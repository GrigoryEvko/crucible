// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D5 fixture #2: mint_federation_admittance rejects when
// the caller does NOT pass a Permission proof at all — e.g. calling
// the factory with only the handshake (or with a non-Permission
// value in the proof slot).  The substrate enforces the proof at
// compile time via its parameter type
// `const LocalCipherPermission& local_permission`; omitting the
// proof or substituting a default-constructed POD must fail
// overload resolution.
//
// Violation: invoke the mint with a default-constructed plain int
// in the proof slot.  The substrate refuses because (a) int does
// not bind to a `const LocalCipherPermission&` reference and (b)
// `LocalCipherPermission` has deleted default and copy ctors
// (linear move-only Permission discipline), so no implicit creation
// of a stand-in proof is possible.
//
// Expected diagnostic: GCC emits a "no matching function for call
// to 'mint_federation_admittance(...)'" or "cannot bind reference"
// overload-resolution failure.

#include <crucible/fixy/Source.h>

// fixy-CR-02 — mint_federation_admittance is [[deprecated]]; suppress
// the diagnostic so it does not interleave with the expected
// overload-resolution failure regex.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace ff = crucible::fixy::source::federation;

struct NegFederationMissingProof_Org {};

int main() {
    auto handshake = ff::make_self_signed_handshake<
        NegFederationMissingProof_Org>();

    // Wrong proof: a plain int.  Cannot bind to
    // `const LocalCipherPermission&` reference; overload resolution
    // must fail.  Substituting a default-constructed
    // LocalCipherPermission is also impossible — Permission<Tag> has
    // no public default ctor (linear discipline requires mint via
    // mint_permission_root, which is auditable per the §XXI
    // Universal Mint Pattern grep rule).
    int not_a_permission = 0;
    (void)ff::mint_federation_admittance<NegFederationMissingProof_Org>(
        not_a_permission, handshake);
    return 0;
}

#pragma GCC diagnostic pop
