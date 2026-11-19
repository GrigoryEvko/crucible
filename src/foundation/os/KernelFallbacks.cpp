// The text of the fallback rows of the kernel floor
// (foundation/os/KernelFallbacks.h), and the report of the text one time in
// the process (report_kernel_fallbacks_once of foundation/os/KernelFloor.h).
// The rows come from flag_floor of foundation/os/KernelFloor.h, and the report
// writes through the sink write of the Report family.

#include <foundation/os/KernelFallbacks.h>

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Format.h>
#include <foundation/core/Report.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

namespace detail {

// The state of the report of the fallback rows in the process.
enum class FallbackReport : std::uint8_t {
    pending,
    written,
};

// The one cell of the report in the process (CRUCIBLE_PROCESS_WIDE of
// foundation/Platform.h).  Each program and each shared library that links
// this file holds a copy, and the dynamic linker binds each copy to one
// object, also across RTLD_LOCAL.  So the first call of
// report_kernel_fallbacks_once in any of them writes the report, and each
// later call writes nothing.
CRUCIBLE_PROCESS_WIDE inline constinit core::Atomic<FallbackReport> kernel_fallback_report_{FallbackReport::pending};

}  // namespace detail

namespace {

// The capacity of one line: the release, the word, the release of the row and
// the fallback text of the longest row.
constexpr std::size_t kLineCapacity = 512;

// The longest text of a row that the report reads, without its zero.  The
// longest fallback text of flag_floor holds about 160 characters.
constexpr std::size_t kRowTextBound = kLineCapacity;

// The text of a string literal of a row, up to its zero or kRowTextBound
// characters.  The bound also keeps the optimizer from making the walk a call
// of strlen of the C library, which the base does not import.  Complexity:
// linear in the length of the text.
[[nodiscard]] core::TextView text_of_row(char const* literal) noexcept {
    std::size_t size = 0;
    while (size < kRowTextBound && literal[size] != '\0') {
        ++size;
    }
    return core::detail::text_over_(literal, size);
}

}  // namespace

core::Result<core::TextView, core::Full<char>>
format_kernel_fallbacks(KernelVersion running, core::FixedText<kernel_fallback_text_capacity>& out) noexcept {
    out.clear();
    bool is_full = false;
    ::foundation::reflect::for_each_enumerator<KernelFlag>([&](KernelFlag flag, auto) noexcept {
        if (is_full || judge_flag(flag, running) != FlagVerdict::fallback) {
            return;
        }
        FlagFloor const row = flag_floor(flag);
        core::FixedText<kLineCapacity> line{};
        bool const is_line_whole =
            core::format(line, "foundation: the kernel release {}.{}.{} lacks {}, which {}.{}.{} added. {}\n",
                         running.major, running.minor, running.patch, text_of_row(row.word), row.release.major,
                         row.release.minor, row.release.patch, text_of_row(row.fallback))
                .is_ok();
        is_full = !is_line_whole || out.append(line.view()).is_err();
    });
    if (is_full) {
        return core::err(core::Full<char>{});
    }
    return out.view();
}

void report_kernel_fallbacks_once(KernelVersion running) noexcept {
    // The first exchange in the process finds the report pending.  Each later
    // call, in this library or in another, finds it written.
    if (detail::kernel_fallback_report_.exchange_acq_rel(detail::FallbackReport::written)
        == detail::FallbackReport::written) {
        return;
    }
    core::FixedText<kernel_fallback_text_capacity> text{};
    // A text that does not fit keeps its first lines, and the report writes
    // them.
    static_cast<void>(format_kernel_fallbacks(running, text));
    if (text.size() == 0) {
        return;
    }
    core::report(core::Sink::Err, "{}", text.view());
}

}  // namespace foundation::os
