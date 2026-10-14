#pragma once

// The argument words of the memory policy door of fixy/os/NumaPlacement.h:
// the policy that binds a range to its nodes, and the flags of mbind.
//
// The values are the values of <linux/mempolicy.h> on x86_64 and aarch64.  The
// door file src/foundation/os/Syscall.cpp compares each one with that header,
// so the memory policy door includes no header of the kernel.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

namespace foundation::os {

// The policy that puts each page of a range on a node of the mask (MPOL_BIND).
inline constexpr int kMemoryPolicyBind = 2;

// The flag of mbind that fails the call when a page of the range is not on a
// node of the policy after the call (MPOL_MF_STRICT).
inline constexpr unsigned kMemoryPolicyStrict = 1U << 0;

// The flag of mbind that moves each page that only this process maps to a
// node of the policy (MPOL_MF_MOVE).
inline constexpr unsigned kMemoryPolicyMove = 1U << 1;

}  // namespace foundation::os
