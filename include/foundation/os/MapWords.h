#pragma once

// The argument words of a memory map of the doors: the protections, the
// flags of a map, and the advice words of madvise.  The values are the
// values of <linux/mman.h> on x86_64 and on aarch64.  The door file
// src/foundation/os/Syscall.cpp holds a check of each value against that
// header.  A door that includes a header of the C library cannot include
// <linux/mman.h>, because some declarations of the two kinds of header
// conflict, so the doors take the values from here.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

namespace foundation::os {

// No access, for a guard page.  PROT_NONE.
inline constexpr int kProtectNone = 0;

// Read access.  PROT_READ.
inline constexpr int kProtectRead = 0x1;

// Write access.  PROT_WRITE.
inline constexpr int kProtectWrite = 0x2;

// Execute access.  PROT_EXEC.
inline constexpr int kProtectExecute = 0x4;

// Read and write access.  PROT_READ | PROT_WRITE.
inline constexpr int kProtectReadWrite = kProtectRead | kProtectWrite;

// A map that each process of the mapped object sees, with its writes in the
// object.  MAP_SHARED.
inline constexpr int kMapShared = 0x01;

// A map whose writes stay in the process.  MAP_PRIVATE.
inline constexpr int kMapPrivate = 0x02;

// A map of memory with no file.  MAP_ANONYMOUS.
inline constexpr int kMapAnonymous = 0x20;

// A map whose pages the kernel locks in memory.  MAP_LOCKED.
inline constexpr int kMapLocked = 0x2000;

// A map whose page tables the kernel fills at the map.  MAP_POPULATE.
inline constexpr int kMapPopulate = 0x8000;

// A map of huge pages.  MAP_HUGETLB.
inline constexpr int kMapHugeTlb = 0x40000;

// The size of each huge page of a map, 2 MiB.  MAP_HUGE_2MB.
inline constexpr int kMapHuge2Mb = 21 << 26;

// A private map of memory with no file.  MAP_PRIVATE | MAP_ANONYMOUS.
inline constexpr int kMapPrivateAnonymous = kMapPrivate | kMapAnonymous;

// The advice of madvise.  Each value is the value of the MADV_ macro of the
// same name.
inline constexpr int kAdviseRandom = 1;
inline constexpr int kAdviseSequential = 2;
inline constexpr int kAdviseWillNeed = 3;
inline constexpr int kAdviseDontNeed = 4;
inline constexpr int kAdviseFree = 8;
inline constexpr int kAdviseRemove = 9;
inline constexpr int kAdviseHugePage = 14;
inline constexpr int kAdviseNoHugePage = 15;
inline constexpr int kAdviseDontDump = 16;
inline constexpr int kAdviseWipeOnFork = 18;
inline constexpr int kAdviseCollapse = 25;

}  // namespace foundation::os
