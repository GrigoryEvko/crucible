// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A P4 program ID reserves zero.  The checked mint of the alias refuses it
// in a constant evaluation.

#include <crucible/cntp/_wip/P4.h>

#include <cstdint>

namespace p4 = crucible::cntp::_wip::p4;

constexpr p4::P4ProgramId bad_program_id = ::fixy::mint_refined<::fixy::non_zero>(std::uint64_t{0});

int main() { return static_cast<int>(bad_program_id.value()); }
