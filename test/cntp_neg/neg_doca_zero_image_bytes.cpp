// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A DOCA program image holds at least one byte.  The checked mint of the
// alias refuses zero in a constant evaluation.

#include <crucible/cntp/_wip/Doca.h>

#include <cstdint>

namespace doca = crucible::cntp::_wip::doca;

constexpr doca::DocaImageBytes bad_image_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{0});

int main() { return static_cast<int>(bad_image_bytes.value()); }
