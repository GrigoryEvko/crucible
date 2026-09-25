// A deployment names the organizations that it admits in one policy.
// The admission mint refuses an organization that the policy does not
// name, even under a context that admits every row the door needs.

#include <fixy/Federation.h>

namespace eff = foundation::effects;
namespace fp = foundation::permissions;
namespace fed = fixy::federation;

struct NegAdmittedOrg {};
struct NegStrangerOrg {};

namespace {

using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

[[maybe_unused]] void attempt(IoCtx const& ctx, fixy::Secret<fixy::siphash::Key>&& key) {
    [[maybe_unused]] auto admission =
        fp::FederationAdmission<NegStrangerOrg>::mint_federation_admission<fed::policy::admit_orgs<NegAdmittedOrg>>(
            ctx, std::move(key));
}

}  // namespace

int main() { return 0; }
