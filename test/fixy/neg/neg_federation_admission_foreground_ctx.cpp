// The admission of a federation peer admits peer tokens, whose row is
// IO, and each admittance takes the local cipher, whose row is IO too.
// A foreground context owns no effect, so the mint refuses it.  The
// requires-clause of the mint is the gate: the constructor of the
// admission is private.
//
// The function takes the context and the key by reference, so nothing
// here builds them.  The failure is the mint's constraint, at the call.

#include <fixy/Federation.h>

namespace eff = foundation::effects;
namespace fp = foundation::permissions;
namespace fed = fixy::federation;

struct NegFederationOrg {};

namespace {

using ForegroundCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;

[[maybe_unused]] void attempt(ForegroundCtx const& foreground, fixy::Secret<fixy::siphash::Key>&& key) {
    [[maybe_unused]] auto admission =
        fp::FederationAdmission<NegFederationOrg>::mint_federation_admission<fed::policy::admit_orgs<NegFederationOrg>>(
            foreground, std::move(key));
}

}  // namespace

int main() { return 0; }
