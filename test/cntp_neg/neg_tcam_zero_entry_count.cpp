// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A table capacity is at least one entry.  The checked mint refuses a zero
// count while it builds the constant.

#include <crucible/cntp/Tcam.h>
#include <fixy/Refined.h>

#include <cstdint>

namespace tcam = crucible::cntp::tcam;

constexpr tcam::TcamEntryCount bad_count = ::fixy::mint_refined<tcam::tcam_entry_range>(std::uint32_t{0});

int main() { return static_cast<int>(bad_count.value()); }
