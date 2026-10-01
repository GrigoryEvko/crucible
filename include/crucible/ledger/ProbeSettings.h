#pragma once

// The two measurement parameters that every probe reads: how many samples,
// and which core.  The probes of ledger/probes/, the refresh daemon of
// RefreshDaemon.h and crucible-hwprobe read and write them through this
// header.  It includes no bench harness, so a translation unit that only
// sets the parameters compiles none of the measurement machinery.

#include <foundation/Platform.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace crucible::ledger {

// ── Measurement parameters ────────────────────────────────────────────
//
// A probe is a plain function pointer taking one argument, so the two
// things a measurement genuinely needs from its invoker arrive here.
// The alternative — widening ProbeFunction — would put a sample count in
// the signature of every future probe including the ones that do not
// take samples, and would make the registration table stop being a
// compile-time constant a reader can follow.
//
// constinit at namespace scope rather than a function-local static: a
// function-local would emit a guard variable and a first-call branch,
// and the house rule bans dynamically-initialized globals outright. The
// aggregate is trivially constructible, so this is one zero-filled
// object in .bss with no initializer to order.

inline constexpr std::size_t kMaxHelperCores = 4;

struct ProbeSettings {
    // Samples per run. Each probe runs twice, so the evidence's sample
    // count is this, not twice this.
    std::uint32_t sample_count = 4096;

    // The core the measurement runs on. Negative asks the bench harness
    // to pick, which prefers an isolated CPU and falls back to the
    // current one.
    int pin_core = -1;

    // Cores for helper threads, used only by the probe that splits work
    // across cores. A negative entry ends the list.
    std::array<int, kMaxHelperCores> helper_cores{-1, -1, -1, -1};

    [[nodiscard]] constexpr std::uint32_t helper_count() const noexcept {
        std::uint32_t found = 0;
        for (const int core : helper_cores) {
            if (core < 0) {
                break;
            }
            ++found;
        }
        return found;
    }
};

// A program sets the probe settings one time, and every probe reads them, in
// whatever shared library it runs.  So the settings are one per process.
CRUCIBLE_PROCESS_WIDE inline constinit ProbeSettings g_probe_settings{};

[[nodiscard]] inline ProbeSettings probe_settings() noexcept { return g_probe_settings; }

inline void set_probe_settings(ProbeSettings settings) noexcept { g_probe_settings = settings; }

}  // namespace crucible::ledger
