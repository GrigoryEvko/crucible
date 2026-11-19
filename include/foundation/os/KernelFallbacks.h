#pragma once

// The report of the fallback rows of the kernel floor at start-up.
//
// A row of the need fallback (THE FLAGS of foundation/os/KernelFloor.h) names
// a word that the caller of a door chooses.  A kernel that lacks the word
// refuses the call, often with einval, and the row states what the caller
// does then.  So a process on such a kernel states each such row one time at
// its start, near the cause, and no door reports it at its first use.
//
// The hook of src/foundation/os/ReportSink.cpp runs after the start-up check
// of the kernel floor, in each program and each shared library that links the
// sink write of the Report family.  It reports each row that the release of
// running_kernel() lacks, one line for each row, in one write to standard
// error.  A kernel that has each word reports nothing.  Each such program or
// library has its own hook, as it has its own start-up check, so a process
// with two of them reports each row two times.

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

// Writes the text of format_kernel_fallbacks for running to standard error in
// one write, or nothing when the release lacks no word.
void report_kernel_fallbacks(KernelVersion running) noexcept;

}  // namespace foundation::os
