// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A ring size is a power of two in [256, 8192]. The checked mint runs
// the bound at compile time, so 1000 stops the constant evaluation.

#include <crucible/cog/NicConfig.h>

namespace nic = crucible::cog::nic;

constexpr nic::NicRingSize bad_ring = ::fixy::mint_refined<nic::ring_size_bound>(std::uint16_t{1000});

int main() { return bad_ring.value(); }
