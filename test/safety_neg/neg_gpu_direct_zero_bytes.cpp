// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A GPUDirect memory region or storage transfer covers at least one byte.
// The checked mint of the alias refuses zero in a constant evaluation.

#include <crucible/cntp/_wip/GpuDirect.h>

#include <cstdint>

namespace gd = crucible::cntp::_wip::gpu_direct;

constexpr gd::GpuDirectByteCount bad_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{0});

int main() { return static_cast<int>(bad_bytes.value()); }
