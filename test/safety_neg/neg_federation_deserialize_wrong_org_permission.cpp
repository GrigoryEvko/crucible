// fixy-CR-06 follow-up: mint_permission_root<FederatedPeer<...>> is
// concept-deleted (see FederationPermission.h §fixy-CR-06).  This
// fixture now seeds its OrgA permission through the admittance
// channel — the only legitimate path — and the wrong-org check on
// deserialize_federation_entry remains the test under verification.

#include <crucible/cipher/FederationProtocol.h>
#include <crucible/permissions/FederationPermission.h>

#include <array>
#include <cstdint>
#include <utility>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

struct OrgA {};
struct OrgB {};

int main() {
    namespace perm = ::crucible::permissions;
    namespace saf  = ::crucible::safety;

    auto local_cipher =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake = perm::make_self_signed_handshake<OrgA>(
        /*peer_key_fp=*/0xDEAD'BEEFULL,
        /*nonce=*/      0xFEED'C0DEULL);
    auto admitted = perm::mint_federation_admittance<
        OrgA, perm::policy::admit_orgs<OrgA>>(local_cipher, handshake);
    auto permission = std::move(*admitted);

    std::array<std::uint8_t, 32> buf{};
    auto view =
        ::crucible::cipher::federation::deserialize_federation_entry<OrgB>(
            permission, buf, std::uint16_t{0});
    (void)view;
    return 0;
}

#pragma GCC diagnostic pop
