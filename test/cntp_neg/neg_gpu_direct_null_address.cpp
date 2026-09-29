// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A GPUDirect plan cannot carry a null GPU virtual address.  The checked
// mint of the alias refuses zero in a constant evaluation.

#include <crucible/cntp/_wip/GpuDirect.h>

#include <cstdint>

namespace gd = crucible::cntp::_wip::gpu_direct;

constexpr gd::GpuVirtualAddress bad_address = ::fixy::mint_refined<::fixy::non_zero>(std::uintptr_t{0});

int main() { return static_cast<int>(bad_address.value()); }
