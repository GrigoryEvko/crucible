#pragma once

// The auxiliary vector door of the base: a read of one entry of the vector
// that the kernel gives each process at its start.
//
// read_auxiliary reads /proc/self/auxv through the file door, and it gives
// the value of one entry.  The vector holds pairs of two words: the type and
// the value.  The type AT_NULL ends it.  The door names each entry that the
// base reads with an enumerator, so no caller gives the door a number.
//
// The door does not call getauxval of the C library.  <sys/auxv.h> declares
// getauxval, and a quarantined file includes that header.  So the door reads
// the file of the kernel, and a caller that reads an entry often keeps the
// value.
//
// This header is in the layer F0_doors of utils/rule-table.txt.  The door file
// is src/foundation/os/Auxiliary.cpp, which includes <linux/auxvec.h>.

#include <foundation/core/Choice.h>

#include <cstdint>

namespace foundation::os {

// The entries of the auxiliary vector that the base reads.
enum class AuxiliaryEntry : std::uint8_t {
    // The size of a page (AT_PAGESZ).
    page_size,
    // The smallest stack in bytes on which the kernel can deliver a signal
    // (AT_MINSIGSTKSZ).  An older kernel does not give this entry.
    minimum_signal_stack,
};

// The value of ENTRY.  The Option is empty when the file cannot be read, and
// when the vector does not hold the entry or holds the value zero for it.
[[nodiscard]] core::Option<std::uint64_t> read_auxiliary(AuxiliaryEntry entry) noexcept;

}  // namespace foundation::os
