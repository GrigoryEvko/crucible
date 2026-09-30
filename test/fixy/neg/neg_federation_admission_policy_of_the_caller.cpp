// The policy of a deployment names the organizations that it admits.  This
// file tries to admit an organization with a policy of its own, a class
// whose admits member answers yes for every organization.  The mint takes
// only a policy that admit_orgs spells, and it reads the organizations
// from the template arguments of that policy, so the class of the caller
// is no policy.

#include <fixy/Federation.h>

namespace eff = foundation::effects;
namespace fp = foundation::permissions;

struct NegAnyPolicyOrg {};

namespace {

struct AnyPolicy {
    template <class Org>
    static constexpr bool admits = true;
};

using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

[[maybe_unused]] void attempt(IoCtx const& ctx, fixy::Secret<fixy::siphash::Key>&& key) {
    [[maybe_unused]] auto admission =
        fp::FederationAdmission<NegAnyPolicyOrg>::mint_federation_admission<AnyPolicy>(ctx, std::move(key));
}

}  // namespace

int main() { return 0; }
