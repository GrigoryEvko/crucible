#pragma once

// The argument words of the memory policy door of fixy/os/NumaPlacement.h:
// the policies of a range and of a thread, and the flags of mbind.
//
// The values are the values of <linux/mempolicy.h> on x86_64 and aarch64.  The
// door file src/foundation/os/Syscall.cpp compares each one with that header,
// so the memory policy door includes no header of the kernel.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

namespace foundation::os {

// The policy of a thread that has no policy of its own: each page on the node
// of the CPU that first touches it (MPOL_DEFAULT).  It takes no mask.
inline constexpr int kMemoryPolicyDefault = 0;

// The policy that takes each page of a range from the node of the mask, and
// from another node when that node is low on free memory (MPOL_PREFERRED).
// The mask holds one node.
inline constexpr int kMemoryPolicyPreferred = 1;

// The policy that puts each page of a range on a node of the mask (MPOL_BIND).
inline constexpr int kMemoryPolicyBind = 2;

// The policy that puts the pages of a range on the nodes of the mask in turn,
// one page on each node (MPOL_INTERLEAVE).
inline constexpr int kMemoryPolicyInterleave = 3;

// The flag of mbind that fails the call when a page of the range is not on a
// node of the policy after the call (MPOL_MF_STRICT).
inline constexpr unsigned kMemoryPolicyStrict = 1U << 0;

// The flag of mbind that moves each page that only this process maps to a
// node of the policy (MPOL_MF_MOVE).
inline constexpr unsigned kMemoryPolicyMove = 1U << 1;

}  // namespace foundation::os
