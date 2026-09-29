// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A TCAM budget asks for at least one entry.  The checked mint of the alias
// refuses zero in a constant evaluation.

#include <crucible/cntp/_wip/P4.h>

#include <cstdint>

namespace p4 = crucible::cntp::_wip::p4;

constexpr p4::P4TcamEntries bad_tcam_entries = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{0});

int main() { return static_cast<int>(bad_tcam_entries.value()); }
