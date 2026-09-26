// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A handle names one virtual function of a declared plan. Its
// constructor from a parent and an index is private, so a caller cannot
// name a function under a parent that no plan admitted.

#include <crucible/cog/SrIov.h>

namespace cog = crucible::cog;
namespace sriov = crucible::cog::sriov;

int main() {
    cog::CogIdentity parent{};
    parent.uuid = cog::Uuid{1, 2};
    parent.kind = cog::CogKind::NicPort;
    sriov::VfHandle forged{parent, sriov::first_vf_index};
    return forged.index().value();
}
