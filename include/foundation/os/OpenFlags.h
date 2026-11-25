#pragma once

// The argument words of an open of the doors: the directory of a relative
// path, the access mode and the flags of an open, the flag of a status read
// through a descriptor, the argument and the resolve flags of openat2, and
// the flags and the seals of a memory file.
//
// The values are the values of <linux/fcntl.h>, <linux/openat2.h> and
// <linux/memfd.h> on x86_64 and aarch64.  The values of O_DIRECTORY,
// O_NOFOLLOW and O_DIRECT are
// different on the two architectures.  The door file
// src/foundation/os/Syscall.cpp compares each value and the layout of
// OpenHow with those headers.  A door that includes a header of the C library
// cannot include <linux/fcntl.h> too, because <fcntl.h> declares the same
// structs, so the doors take the values from here.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

#include <cstdint>

namespace foundation::os {

// The directory of a relative path of a call of the *at family: the current
// directory of the process (AT_FDCWD).
inline constexpr int kAtCurrentDirectory = -100;

// A status read of the *at family reads a symbolic link itself, and does not
// follow it (AT_SYMLINK_NOFOLLOW).
inline constexpr int kAtSymlinkNoFollow = 0x100;

// An unlinkat removes an empty directory, and no entry of another kind
// (AT_REMOVEDIR).  A directory that holds an entry gives ENOTEMPTY.
inline constexpr int kAtRemoveDirectory = 0x200;

// An open for reading only (O_RDONLY).
inline constexpr int kOpenReadOnly = 0;

// An open for writing only (O_WRONLY).
inline constexpr int kOpenWriteOnly = 01;

// An open for reading and writing (O_RDWR).
inline constexpr int kOpenReadWrite = 02;

// The open makes a regular file when the path names no file (O_CREAT).  The
// kernel then reads the permission bits of the new file from the mode word.
inline constexpr int kOpenCreate = 0100;

// With kOpenCreate, the open fails with EEXIST when the path names an entry,
// also a symbolic link (O_EXCL).  So the file of a successful open is a new
// file that no other open of the same path made.
inline constexpr int kOpenExclusive = 0200;

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

// The open gives a descriptor that names the entry and reads no data of it
// (O_PATH).  A call of the *at family reads such a descriptor of a directory
// as its directory.  With O_NOFOLLOW, a symbolic link at the last part of the
// path opens as itself.
inline constexpr int kOpenPath = 010000000;

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

// The open makes a regular file with no name in the directory of the path
// (O_TMPFILE).  The word holds the bit of the anonymous file and the bit of
// O_DIRECTORY, so a kernel with no O_TMPFILE refuses the open of a
// directory.  The kernel reads the permission bits of the new file from the
// mode word, and the file goes away at its last close.
inline constexpr int kOpenTemporary = 020000000 | kOpenDirectory;

// The resolve flags of openat2.  An open with kResolveBeneath fails with
// EXDEV when a part of the path names an entry outside the directory of the
// descriptor: an absolute path, a ".." part that climbs above the directory,
// or a symbolic link whose target is outside it (RESOLVE_BENEATH).  An open
// with kResolveNoMagicLinks fails with ELOOP at a magic link of procfs, such
// as /proc/self/root (RESOLVE_NO_MAGICLINKS).
inline constexpr std::uint64_t kResolveNoMagicLinks = 0x02;
inline constexpr std::uint64_t kResolveBeneath = 0x08;

// The argument of openat2 (struct open_how of <linux/openat2.h>): the open
// flags, the permission bits of a new file, and the resolve flags.  The
// kernel refuses permission bits with EINVAL when the flags hold neither
// kOpenCreate nor kOpenTemporary.
struct OpenHow {
    std::uint64_t flags = 0;
    std::uint64_t mode = 0;
    std::uint64_t resolve = 0;
};

// The flags of memfd_create.  The descriptor closes when a thread of the
// process starts a program (MFD_CLOEXEC).  The file takes seals
// (MFD_ALLOW_SEALING).  No program of the file can start, and no change of
// its mode can make it start (MFD_NOEXEC_SEAL).
inline constexpr unsigned int kMemoryFileCloseOnExec = 0x1;
inline constexpr unsigned int kMemoryFileAllowSeals = 0x2;
inline constexpr unsigned int kMemoryFileNoExecSeal = 0x8;

// The command of fcntl that adds seals to a memory file (F_ADD_SEALS), and
// the seals.  The file takes no later seal (F_SEAL_SEAL), and its size does
// not decrease (F_SEAL_SHRINK).
inline constexpr int kFcntlAddSeals = 1033;
inline constexpr unsigned int kSealSeal = 0x1;
inline constexpr unsigned int kSealShrink = 0x2;

}  // namespace foundation::os
