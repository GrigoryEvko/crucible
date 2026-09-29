// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// An SR-IOV plan is startup work. The foreground row is empty, so the
// mint refuses the dispatch thread. A foreground context comes only from
// the producer claim, so the fixture takes one as a parameter.

#include <crucible/cog/SrIov.h>

namespace cog = crucible::cog;
namespace sriov = crucible::cog::sriov;

[[maybe_unused]] static int mint_from_foreground(::fixy::HotFgCtx const& fg, crucible::cntp::NicInterfaceName iface) {
    auto result =
        sriov::mint_sriov_plan(fg, cog::CogIdentity{}, cog::NicPortTargetCaps{}, iface, *sriov::admit_vf_count(1));
    return result.has_value() ? 0 : 1;
}

int main() { return 0; }
