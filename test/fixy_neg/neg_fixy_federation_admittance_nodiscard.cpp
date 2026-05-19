// fixy_neg: fixy::source::federation::mint_federation_admittance
// must preserve the substrate's [[nodiscard]] attribute through its
// `using ::crucible::permissions::mint_federation_admittance;`
// re-export.  fixy-A4-027 audit flagged this as a §XXI mint discipline
// risk: a future refactor that introduces a thin forwarder lambda
// without [[nodiscard]] would silently drop the attribute, defeating
// the AdmittanceError result-must-be-checked contract.
//
// This fixture EMPIRICALLY witnesses preservation:
//   1. Call the function through `fixy::source::federation::`.
//   2. DISCARD the std::expected return.
//   3. Expect compile to fail with -Werror=unused-result.
//
// If the attribute is lost (e.g. forwarder added without
// [[nodiscard]]), this fixture would COMPILE successfully and the
// neg-compile driver would flag "fixture compiled successfully" as a
// regression — exactly the audit's intent.
//
// Expected diagnostic: "unused-result".

#include <crucible/fixy/Source.h>
#include <crucible/permissions/Permission.h>

// `mint_federation_admittance` is [[deprecated]] in V1 (fixy-CR-02
// placeholder verifier).  Suppress to isolate the [[nodiscard]] axis.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace ffed = crucible::fixy::source::federation;

struct TestOrg {};

constexpr auto handshake = ffed::make_self_signed_handshake<TestOrg>(
    ffed::PeerKeyFingerprint{0xC0FFEEu},
    ffed::Nonce{0xBEEFu});

int main() {
    auto local =
        crucible::safety::mint_permission_root<ffed::LocalCipherTag>();

    // ── Load-bearing discard — MUST trigger -Werror=unused-result ──
    ffed::mint_federation_admittance<TestOrg>(local, handshake);

    return 0;
}

#pragma GCC diagnostic pop
