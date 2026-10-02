// The fixtures that the probe tests of the hardware-capability ledger share:
// a host that is fit to measure, a host that is not, the context under which
// a probe maps and pins, and the cache root of a test that commits to the
// store.

#pragma once

#include <crucible/ledger/Ledger.h>

#include <fixy/Ctx.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <unistd.h>

namespace ledger_probe_fixtures {

// The cache root of one test process.  The store finds its root through
// XDG_CACHE_HOME, so a test that commits sets that variable to a fresh
// directory, and the store never writes to the real ledger of the machine.
//
// The destructor removes the directory with everything in it, and it puts
// back the value that the variable had before.  An abort runs no destructor,
// so the name of the directory holds the process id.  The constructor removes
// each directory of this prefix whose process no longer runs.  A directory of
// a live process stays, also when that process is a different test.
class ScopedCacheHome final {
public:
    ScopedCacheHome() {
        remove_directories_of_ended_processes_();
        if (const char* earlier = std::getenv("XDG_CACHE_HOME"); earlier != nullptr) {
            had_earlier_ = true;
            earlier_ = earlier;
        }
        std::string pattern =
            std::string{kDirectory} + "/" + std::string{kPrefix} + std::to_string(::getpid()) + "-XXXXXX";
        if (const char* made = ::mkdtemp(pattern.data()); made != nullptr) {
            path_ = made;
        }
        if (path_.empty() || ::setenv("XDG_CACHE_HOME", path_.c_str(), 1) != 0) {
            std::abort();
        }
    }

    ~ScopedCacheHome() {
        if (had_earlier_) {
            (void)::setenv("XDG_CACHE_HOME", earlier_.c_str(), 1);
        } else {
            (void)::unsetenv("XDG_CACHE_HOME");
        }
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ScopedCacheHome(const ScopedCacheHome&) = delete("one directory has one owner");
    ScopedCacheHome& operator=(const ScopedCacheHome&) = delete("one directory has one owner");

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    static constexpr std::string_view kDirectory = "/tmp";
    static constexpr std::string_view kPrefix = "crucible-probe-test-";

    // A name of this prefix is the process id, a dash, and the six characters
    // of mkdtemp.  Another name of the prefix is not one of these, and it
    // stays.  Complexity: linear in the number of entries of the directory.
    static void remove_directories_of_ended_processes_() {
        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{std::filesystem::path{kDirectory}, error}) {
            const std::string name = entry.path().filename().string();
            if (!std::string_view{name}.starts_with(kPrefix)) continue;
            const std::string_view rest = std::string_view{name}.substr(kPrefix.size());
            const std::size_t dash = rest.find('-');
            if (dash == 0 || dash == std::string_view::npos || rest.size() - dash - 1 != 6) continue;
            pid_t owner = 0;
            bool is_number = true;
            for (const char digit : rest.substr(0, dash)) {
                if (digit < '0' || digit > '9' || owner > 99999999) {
                    is_number = false;
                    break;
                }
                owner = owner * 10 + (digit - '0');
            }
            if (!is_number || owner == ::getpid()) continue;
            if (::kill(owner, 0) != 0 && errno == ESRCH) {
                std::error_code ignored;
                std::filesystem::remove_all(entry.path(), ignored);
            }
        }
    }

    std::string path_;
    std::string earlier_;
    bool had_earlier_ = false;
};

[[nodiscard]] inline crucible::ledger::CompetenceReport fit_host() noexcept {
    crucible::ledger::CompetenceReport report{};
    report.isolated_core_count = 8;
    report.online_sibling_count = 0;
    report.load_average_milli = 1000;
    report.allowed_cpu_count = 384;
    report.machine_cpu_count = 384;
    report.perf_event_paranoid = 2;
    report.scaling_min_freq_khz = 4510205;
    report.scaling_max_freq_khz = 4510205;
    report.governor_is_performance = false;
    report.defects = crucible::ledger::derive_defects(report, true);
    return report;
}

[[nodiscard]] inline crucible::ledger::CompetenceReport unfit_host() noexcept {
    crucible::ledger::CompetenceReport report = fit_host();
    report.online_sibling_count = 4;  // an isolated core shares its pipeline
    report.defects = crucible::ledger::derive_defects(report, true);
    return report;
}

// A probe maps and pins under the context of the store.
inline constexpr crucible::ledger::LedgerIoCtx probe_ctx{::foundation::effects::testing::bg()};

}  // namespace ledger_probe_fixtures
