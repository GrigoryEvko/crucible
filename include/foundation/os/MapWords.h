#pragma once

// The argument words of a memory map of the doors: the protections and the
// flags of a private map of memory with no file.  The values are the values
// of <linux/mman.h> on x86_64 and on aarch64.  The door file
// src/foundation/os/Syscall.cpp holds a check of each value against that
// header.  A door that includes a header of the C library cannot include
// <linux/mman.h>, because some declarations of the two kinds of header
// conflict, so the doors take the values from here.

namespace foundation::os {

// No access, for a guard page.  PROT_NONE.
inline constexpr int kProtectNone = 0;

// Read and write access.  PROT_READ | PROT_WRITE.
inline constexpr int kProtectReadWrite = 0x1 | 0x2;

// A private map of memory with no file.  MAP_PRIVATE | MAP_ANONYMOUS.
inline constexpr int kMapPrivateAnonymous = 0x02 | 0x20;

}  // namespace foundation::os
