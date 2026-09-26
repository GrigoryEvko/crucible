// A peer token of one organization does not open the permissioned decode
// for another organization.  The token comes from the federation door of
// fixy/Federation.h, the one route to a peer token, so the refusal below
// is the organization check of deserialize_federation_entry and nothing
// else.

#include <crucible/cipher/FederationProtocol.h>
#include <fixy/Federation.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

struct OrgA {};
struct OrgB {};

int main() {
    namespace fe = ::foundation::effects;
    namespace fp = ::foundation::permissions;
    namespace door = ::fixy::federation;
    namespace sh = ::fixy::siphash;
    using IoCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::IO>>;
    using Admission = fp::FederationAdmission<OrgA>;

    const IoCtx ctx{fe::testing::bg()};
    sh::Key shared_key{};
    Admission admission = Admission::mint_federation_admission<door::policy::admit_orgs<OrgA>>(
        ctx, ::fixy::mint_secret<sh::Key>(shared_key));
    auto local_cipher = fp::mint_permission_root<door::LocalCipherTag>(ctx);
    auto [peer_key, handshake] = door::sign_handshake<OrgA>(::fixy::mint_secret<sh::Key>(shared_key),
                                                             door::PeerKeyFingerprint{0xDEADBEEFULL},
                                                             door::Nonce{1});
    auto [returned_cipher, admitted] = admission.mint_federation_admittance(std::move(local_cipher), handshake);
    (void)peer_key;
    (void)returned_cipher;

    std::array<std::uint8_t, 32> buf{};
    auto view = ::crucible::cipher::federation::deserialize_federation_entry<OrgB>(*admitted, buf, std::uint16_t{0});
    (void)view;
    return 0;
}
