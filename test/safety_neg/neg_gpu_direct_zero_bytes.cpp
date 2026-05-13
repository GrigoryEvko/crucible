// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-132. A GPUDirect MR or storage transfer must
// cover a positive byte count.

#include <crucible/cntp/_wip/GpuDirect.h>

namespace gd = crucible::cntp::_wip::gpu_direct;

constexpr gd::GpuDirectByteCount bad_bytes{std::uint64_t{0}};

int main() {
    return static_cast<int>(bad_bytes.value());
}
