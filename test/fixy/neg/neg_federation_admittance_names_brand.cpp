// Each admitted peer token takes a fresh brand from its call site.  A
// caller that names a brand could make a second token of an identity
// that it already holds, so an explicit template argument lands in the
// pack that the mint requires to be empty.

#include <fixy/Federation.h>

namespace fp = foundation::permissions;
namespace fed = fixy::federation;

struct NegBrandOrg {};

namespace {

struct HeldBrand {};
struct LocalCipherBrand {};

[[maybe_unused]] void attempt(fp::FederationAdmission<NegBrandOrg>& admission,
                              fp::Permission<fed::LocalCipherTag, LocalCipherBrand>&& local_cipher,
                              fed::FederationHandshake const& handshake) {
    [[maybe_unused]] auto admitted = admission.mint_federation_admittance<HeldBrand>(std::move(local_cipher), handshake);
}

}  // namespace

int main() { return 0; }
