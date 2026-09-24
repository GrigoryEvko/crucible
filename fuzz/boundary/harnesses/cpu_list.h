#pragma once

// A CPU list from sysfs or from /proc/self/status, such as "0-3,8,10-11".
// The parsers must survive any text, and a parsed list stays small: a range
// such as "0-4294967295" that expands to billions of entries would take the
// memory of the host.  The list is sorted, holds no duplicate, and names
// only ids below the kernel ceiling.

#include "../harness.h"

#include <crucible/warden/CpuTopology.h>

#include <algorithm>

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
    const std::string_view text = as_text(bytes);
    const auto claim_list = [](const std::vector<int>& cpus) {
        CRUCIBLE_FUZZ_CLAIM("cpu_list", cpus.size() <= kMaxCpuCount);
        CRUCIBLE_FUZZ_CLAIM("cpu_list", std::ranges::is_sorted(cpus));
        CRUCIBLE_FUZZ_CLAIM("cpu_list", std::ranges::adjacent_find(cpus) == cpus.end());
        for (const int cpu : cpus) {
            CRUCIBLE_FUZZ_CLAIM("cpu_list", cpu >= 0 && static_cast<std::size_t>(cpu) < kMaxCpuCount);
        }
    };
    claim_list(warden::detail::parse_cpulist(text));
    claim_list(warden::detail::parse_cpus_allowed_list(text));
}

}  // namespace crucible::fuzz::boundary
