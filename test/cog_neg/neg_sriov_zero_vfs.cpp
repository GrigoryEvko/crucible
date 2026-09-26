// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A plan asks for at least one virtual function. Zero is the disabled
// state that disable() reaches, so the checked mint of a count stops the
// constant evaluation at zero.

#include <crucible/cog/SrIov.h>

namespace sriov = crucible::cog::sriov;

constexpr sriov::VfCount bad_count = ::fixy::mint_refined<sriov::vf_count_bound>(std::uint16_t{0});

int main() { return static_cast<int>(bad_count.value()); }
