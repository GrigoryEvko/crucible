// The address of the admittance with a target type deduces each template
// parameter that the type names, also the brand of the peer token.  A caller
// would then admit a second peer with the brand of a token that it holds.
// The result type names the brand in a context that deduction cannot set,
// so the target type names no template that matches.

#include <fixy/Federation.h>
#include <foundation/Quarantine.h>

#include <utility>

namespace fp = foundation::permissions;
namespace fed = fixy::federation;

struct NegAddressOrg {};

namespace {

struct LocalCipherBrand {};
using Admission = fp::FederationAdmission<NegAddressOrg>;
using LocalToken = fp::Permission<fed::LocalCipherTag, LocalCipherBrand>;

[[maybe_unused]] void attempt(Admission& admission, LocalToken&& local_cipher,
                              fed::FederationHandshake const& handshake) {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture takes the address of a member mint with a target type")
    using Held = decltype(admission.mint_federation_admittance(std::move(local_cipher), handshake));
    [[maybe_unused]] Held (Admission::*mint)(LocalToken&&, fed::FederationHandshake const&) noexcept =
        &Admission::mint_federation_admittance;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

}  // namespace

int main() { return 0; }
