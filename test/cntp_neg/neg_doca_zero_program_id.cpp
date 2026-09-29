// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A DOCA program ID reserves zero.  The checked mint of the alias refuses
// it in a constant evaluation.

#include <crucible/cntp/_wip/Doca.h>

#include <cstdint>

namespace doca = crucible::cntp::_wip::doca;

constexpr doca::DocaProgramId bad_program_id = ::fixy::mint_refined<::fixy::non_zero>(std::uint64_t{0});

int main() { return static_cast<int>(bad_program_id.value()); }
