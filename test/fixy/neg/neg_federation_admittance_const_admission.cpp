// An admittance records the nonce of the handshake in the replay window
// of the admission.  A const admission cannot record it, so the mint is
// not a const member, and a const admission admits nobody.

#include <fixy/Federation.h>

namespace fp = foundation::permissions;
namespace fed = fixy::federation;

struct NegConstOrg {};

namespace {

struct LocalCipherBrand {};

[[maybe_unused]] void attempt(fp::FederationAdmission<NegConstOrg> const& admission,
                              fp::Permission<fed::LocalCipherTag, LocalCipherBrand>&& local_cipher,
                              fed::FederationHandshake const& handshake) {
    [[maybe_unused]] auto admitted = admission.mint_federation_admittance(std::move(local_cipher), handshake);
}

}  // namespace

int main() { return 0; }
