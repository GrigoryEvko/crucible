// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-132. GPUDirect plans cannot carry a null GPU
// virtual address; null must be rejected at the refinement boundary.

#include <crucible/cntp/_wip/GpuDirect.h>

namespace gd = crucible::cntp::_wip::gpu_direct;

constexpr gd::GpuVirtualAddress bad_address{std::uintptr_t{0}};

int main() {
    return static_cast<int>(bad_address.value());
}
