#pragma once

// The text of the fallback rows of the kernel floor that a release lacks.
//
// A row of the need fallback (the flags of foundation/os/KernelFloor.h) names
// a word that the caller of a door chooses.  A kernel that lacks the word
// refuses the call, often with einval, and the row states what the caller
// does then.  So a process on such a kernel states each such row one time,
// near the cause, and no door reports it at its first use.
//
// The init door of foundation/effects/Effect.h writes the report, through
// report_kernel_fallbacks_once of foundation/os/KernelFloor.h, one time in the
// process: one line for each row that the release of running_kernel() lacks,
// in one write to standard error.  A kernel that has each word reports
// nothing.  The load of a program or of a shared library writes nothing.
//
// format_kernel_fallbacks is the query of the text.  Each program and each
// shared library can read the text of any release with it, at any time, also
// before the init door and in a process that never opens the door.

#include <foundation/core/Choice.h>
#include <foundation/core/Full.h>
#include <foundation/core/Text.h>
#include <foundation/os/KernelFloor.h>

#include <cstddef>

namespace foundation::os {

// The capacity of the text of the report.  The lines of each release that the
// floor admits fit it with room to spare.
inline constexpr std::size_t kernel_fallback_text_capacity = 4096;

// Writes into out, from its start, one line for each row of the need fallback
// that the release running lacks, in the order of KernelFlag: the release, the
// word of the row, the release that added it, and the fallback of the row.
// The view of the text, or Full<char> with the first lines in out.  A release
// that lacks no word gives the empty text.  Complexity: linear in the rows.
[[nodiscard]] core::Result<core::TextView, core::Full<char>>
format_kernel_fallbacks(KernelVersion running, core::FixedText<kernel_fallback_text_capacity>& out) noexcept;

}  // namespace foundation::os
