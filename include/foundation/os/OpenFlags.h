#pragma once

// The argument words of an open of the doors: the directory of a relative
// path, the access mode and the flags of an open, and the flag of a status
// read through a descriptor.
//
// The values are the values of <linux/fcntl.h> on x86_64 and aarch64.  The
// values of O_DIRECTORY, O_NOFOLLOW and O_DIRECT are different on the two
// architectures.  The door file src/foundation/os/Syscall.cpp compares each
// value with that header.  A door that includes a header of the C library
// cannot include <linux/fcntl.h> too, because <fcntl.h> declares the same
// structs, so the doors take the values from here.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

namespace foundation::os {

// The directory of a relative path of a call of the *at family: the current
// directory of the process (AT_FDCWD).
inline constexpr int kAtCurrentDirectory = -100;

// A status read of the *at family reads a symbolic link itself, and does not
// follow it (AT_SYMLINK_NOFOLLOW).
inline constexpr int kAtSymlinkNoFollow = 0x100;

// An open for reading only (O_RDONLY).
inline constexpr int kOpenReadOnly = 0;

// An open for writing only (O_WRONLY).
inline constexpr int kOpenWriteOnly = 01;

// An open for reading and writing (O_RDWR).
inline constexpr int kOpenReadWrite = 02;

// The open makes a regular file when the path names no file (O_CREAT).  The
// kernel then reads the permission bits of the new file from the mode word.
inline constexpr int kOpenCreate = 0100;

// The open cuts a regular file that it opens for writing to a length of zero
// (O_TRUNC).
inline constexpr int kOpenTruncate = 01000;

// Each write puts its bytes at the end of the file (O_APPEND).
inline constexpr int kOpenAppend = 02000;

// Each write returns when its data is on stable storage, with the metadata
// that a read of the data needs (O_DSYNC).
inline constexpr int kOpenDataSync = 010000;

// Each write returns when its data and all its metadata are on stable storage
// (O_SYNC).
inline constexpr int kOpenFullSync = 04010000;

// The descriptor closes when a thread of the process starts a program
// (O_CLOEXEC), so no started program gets it.
inline constexpr int kOpenCloseOnExec = 02000000;

// The open fails with ENOTDIR when the path names no directory (O_DIRECTORY).
// The open fails with ELOOP when the last part of the path is a symbolic link
// (O_NOFOLLOW).  The reads and the writes move the data between the device
// and the buffer of the caller, with no page cache (O_DIRECT).
#if defined(__x86_64__)
inline constexpr int kOpenDirectory = 0200000;
inline constexpr int kOpenNoFollow = 0400000;
inline constexpr int kOpenDirect = 040000;
#elif defined(__aarch64__)
inline constexpr int kOpenDirectory = 040000;
inline constexpr int kOpenNoFollow = 0100000;
inline constexpr int kOpenDirect = 0200000;
#else
#error "foundation/os/OpenFlags.h gives the flags of x86_64 and aarch64 only.  Add the values of the new architecture."
#endif

}  // namespace foundation::os
