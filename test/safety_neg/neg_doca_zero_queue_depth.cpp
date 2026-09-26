// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A DPU queue holds at least one entry.  The checked mint of the alias
// refuses zero in a constant evaluation.

#include <crucible/cntp/_wip/Doca.h>

#include <cstdint>

namespace doca = crucible::cntp::_wip::doca;

constexpr doca::DocaQueueDepth bad_queue_depth = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{0});

int main() { return static_cast<int>(bad_queue_depth.value()); }
