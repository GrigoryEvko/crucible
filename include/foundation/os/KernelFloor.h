#pragma once

// The release of the kernel that the raw system calls of the base need, and
// the start-up check of it.
//
// A raw call has no fallback of the C library, so an older kernel gives
// ENOSYS where the C library gave a result.  minimum_kernel() gives the
// largest release that a call of the enumeration Syscall needs on the
// architecture of the build.  The door file src/foundation/os/Syscall.cpp
// gives the release of each enumerator and derives the minimum from them.  A
// new enumerator with a newer release raises the minimum with no other edit.
//
// The start-up check is a function of that door file that the loader runs
// before main, and before the constructors of the other objects, in each
// program and each shared library that links the primitive.  It reads the
// release of the running kernel one time with uname.  An older kernel, or a
// release text that does not start with MAJOR.MINOR, ends the process with
// a message on standard error that names the two releases.  The check holds
// no state.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

#include <compare>
#include <cstdint>

namespace foundation::os {

// A release of the kernel, as uname gives it: MAJOR.MINOR.PATCH.
struct KernelVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;

    friend constexpr std::strong_ordering operator<=>(KernelVersion const&, KernelVersion const&) noexcept = default;
};

// The largest release that a call of the enumeration Syscall needs on the
// architecture of the build.
[[nodiscard]] KernelVersion minimum_kernel() noexcept;

// The verdict of the start-up check for one release text.
enum class KernelVerdict : std::uint8_t {
    admitted,
    too_old,
    unreadable,
};

// The verdict for the release text RELEASE against minimum_kernel().  The
// check reads RELEASE up to its zero, and at most 64 characters, which is the
// length of the release field of uname.  A text that does not start with
// MAJOR.MINOR is unreadable, and so is a part that does not fit in 16 bits.
// A missing patch number, or a patch number that a suffix follows, reads as
// written: "6.1" is 6.1.0, and "7.1.4-202.fc44" is 7.1.4.
[[nodiscard]] KernelVerdict judge_release(char const* release) noexcept;

}  // namespace foundation::os
