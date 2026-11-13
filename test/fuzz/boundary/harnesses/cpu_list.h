#pragma once

// A CPU list from sysfs or from /proc/thread-self/status, such as "0-3,8,10-11".
// The parsers must survive any text, and a parsed set stays small: a range
// such as "0-4294967295" that expands to billions of entries would take the
// memory of the host.  The set names only ids below the kernel ceiling, and
// its loop gives each id one time, in ascending order.

#include "../harness.h"

#include <crucible/warden/CpuTopology.h>
#include <foundation/os/PathText.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_cpu_list() {
    return {
        text_bytes("0-3,8,10-11\n"),
        text_bytes("Cpus_allowed_list:\t0-5,7\n"),
        text_bytes("0-8191\n"),
        // Regressions.  A range that expanded to millions of ids, one that
        // wrapped the loop counter, a number past the range of int, and a
        // list that ends in a digit, which the parser read past its end.
        text_bytes("0-8191911\n"),
        text_bytes("0-2147483647"),
        text_bytes("99999999999999999999"),
        text_bytes("12"),
    };
}

inline void run_cpu_list(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMaxCpuCount = warden::detail::kMaxCpuCount;
    // A TextView comes only from an owner of the base, so the bytes go into
    // a path text.  A path text refuses a zero byte, so the harness drops
    // each one, and a byte past the capacity ends the text.
    ::foundation::os::PathText<4096> text{};
    for (const std::uint8_t byte : bytes) {
        if (byte == 0) continue;
        if (text.append(static_cast<char>(byte)).is_err()) break;
    }
    const auto claim_set = [](const warden::CpuSet& cpus) {
        CRUCIBLE_FUZZ_CLAIM("cpu_list", cpus.size() <= kMaxCpuCount);
        int previous = -1;
        for (const int cpu : cpus) {
            CRUCIBLE_FUZZ_CLAIM("cpu_list", cpu > previous);
            CRUCIBLE_FUZZ_CLAIM("cpu_list", cpu >= 0 && static_cast<std::size_t>(cpu) < kMaxCpuCount);
            previous = cpu;
        }
    };
    claim_set(warden::detail::parse_cpulist(text.c_text().view()));
    claim_set(warden::detail::parse_cpus_allowed_list(text.c_text().view()));
}

}  // namespace crucible::fuzz::boundary
