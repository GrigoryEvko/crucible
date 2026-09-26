// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A P4 source holds at least one byte.  The checked mint of the alias
// refuses zero in a constant evaluation.

#include <crucible/cntp/_wip/P4.h>

#include <cstdint>

namespace p4 = crucible::cntp::_wip::p4;

constexpr p4::P4SourceBytes bad_source_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{0});

int main() { return static_cast<int>(bad_source_bytes.value()); }
