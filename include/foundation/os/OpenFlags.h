#pragma once

// The argument words of an open of the doors: the directory of a relative
// path, and the flags of an open for reading.
//
// The values are the values of <linux/fcntl.h> on x86_64 and aarch64.  The
// value of O_DIRECTORY is different on the two architectures.  The door file
// src/foundation/os/Syscall.cpp compares each value with that header.
// A door that includes a header of the C library cannot include
// <linux/fcntl.h> too, because <fcntl.h> declares the same structs, so the
// doors take the values from here.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

namespace foundation::os {

// The directory of a relative path of a call of the *at family: the current
// directory of the process (AT_FDCWD).
inline constexpr int kAtCurrentDirectory = -100;

// An open for reading only (O_RDONLY).
inline constexpr int kOpenReadOnly = 0;

// The descriptor closes when a thread of the process starts a program
// (O_CLOEXEC), so no started program gets it.
inline constexpr int kOpenCloseOnExec = 02000000;

// The open fails with ENOTDIR when the path names no directory (O_DIRECTORY).
#if defined(__x86_64__)
inline constexpr int kOpenDirectory = 0200000;
#elif defined(__aarch64__)
inline constexpr int kOpenDirectory = 040000;
#else
#error "foundation/os/OpenFlags.h gives the flags of x86_64 and aarch64 only.  Add the values of the new architecture."
#endif

}  // namespace foundation::os
