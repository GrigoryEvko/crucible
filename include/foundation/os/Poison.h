#pragma once

// The door of AddressSanitizer for memory that a program manages itself.
//
// An arena gives out bytes of its blocks, and the end of an epoch makes each
// of those bytes dead while the block stays allocated for reuse.  The
// allocator of AddressSanitizer does not see that end, so a read through a
// pointer from the dead epoch reads a live block and no report comes.  The
// owner of the bytes tells the sanitizer: poison_bytes marks a run of bytes
// as not addressable, and unpoison_bytes marks a run as addressable again
// when the owner gives it out.  A load or a store of a poisoned byte then
// ends the process with a report of AddressSanitizer.
//
// Only a build with AddressSanitizer has a body of the two functions, in
// src/foundation/os/Poison.cpp.  In each other build they are empty inline
// functions, so a call costs nothing.  The flag -fsanitize=address is a flag
// of each target of a build, so each unit of one build sees one arm.

#include <cstddef>

namespace foundation::os {

#if defined(__SANITIZE_ADDRESS__)

// Marks the count bytes from first as not addressable.  The sanitizer keeps
// the state in units of 8 bytes, so it can leave some bytes at the two ends
// of a run that is not aligned to 8 addressable.
void poison_bytes(void const* first, std::size_t count) noexcept;

// Marks the count bytes from first as addressable.
void unpoison_bytes(void const* first, std::size_t count) noexcept;

#else

inline void poison_bytes(void const*, std::size_t) noexcept {}
inline void unpoison_bytes(void const*, std::size_t) noexcept {}

#endif

}  // namespace foundation::os
