#pragma once

// The scratch directory of a test process, and the cache root of a test that
// commits to a store.
//
// make_scratch_directory sweeps the trees that ended processes left under the
// temporary directory of the host (THE SWEEP of fixy/os/Fs.h), and then makes
// a temporary directory of the base (THE TEMPORARY DIRECTORY).  The end of the
// owner removes its tree.  An abort runs no end, so a test that aborts leaves
// its tree only until the sweep of a later test.  The sweep keeps each tree of
// a live process.  make_scratch_path gives the path of an entry under the
// scratch directory.
//
// ScopedCacheHome sets XDG_CACHE_HOME through the C library, because the
// environment door of the base reads the environment one time at the start and
// has no set door.  It holds a ScratchDir.
//
// A ScratchDir is a directory under /tmp that the object removes with
// everything in it when it ends.  An abort runs no destructor, so the name of
// the directory holds the process id: PREFIX-PID-XXXXXX, where mkdtemp gives
// the last six characters.  The constructor removes each directory of the same
// prefix whose process no longer runs.  So a test that aborts leaves its
// directory only until the next run of a test with that prefix.  A directory
// of a live process stays, also when that process is a different test.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PathText.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace crucible::test {

// The path of an entry under the scratch directory.
using ScratchPath = ::foundation::os::PathText<::fixy::kPathBytes>;

// The scratch directory of a test (the comment at the top of this file).  The
// sweep removes only the trees of other processes, so its error makes no
// error of the scratch directory.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsTemporaryDirectoryMint<Ctx>
[[nodiscard]] ::fixy::Result<::fixy::fs::TemporaryDirectory, ::foundation::os::Errno>
make_scratch_directory(Ctx const& ctx) noexcept {
    static_cast<void>(::fixy::fs::sweep_temporary_directories(ctx).is_ok());
    return ::fixy::fs::mint_temporary_directory(ctx).map(
        [](::fixy::Linear<::fixy::fs::TemporaryDirectory> owner) noexcept { return std::move(owner).consume(); });
}

// The path of the entry name under the scratch directory: its path, a slash
// and the name.
[[nodiscard]] inline ScratchPath make_scratch_path(::fixy::fs::TemporaryDirectory const& scratch,
                                                   ::fixy::TextView name) noexcept {
    ScratchPath path{};
    path.append(scratch.path()).expect("the path of the scratch directory fits a path");
    path.append('/').expect("a path under the scratch directory fits a path");
    path.append(name).expect("a path under the scratch directory fits a path");
    return path;
}

class ScratchDir final {
public:
    // The directory under which each scratch directory is made.
    static constexpr std::string_view kDirectory = "/tmp";

    // Makes the directory kDirectory/PREFIX-PID-XXXXXX.  The prefix must not
    // be empty.  When mkdtemp fails, is_ready() is false and path() is empty.
    explicit ScratchDir(std::string_view prefix) {
        remove_directories_of_ended_processes(prefix);
        std::string pattern = std::string{kDirectory} + "/" + std::string{prefix} + "-"
                            + std::to_string(static_cast<long>(::getpid())) + "-XXXXXX";
        if (const char* made = ::mkdtemp(pattern.data()); made != nullptr) {
            path_ = made;
        }
    }

    ~ScratchDir() {
        if (path_.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ScratchDir(const ScratchDir&) = delete("one directory has one owner, and the owner removes it one time");
    ScratchDir& operator=(const ScratchDir&) = delete("one directory has one owner, and the owner removes it one time");

    [[nodiscard]] bool is_ready() const noexcept { return !path_.empty(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::string file(std::string_view name) const { return path_ + "/" + std::string{name}; }

    // Removes each directory kDirectory/PREFIX-PID-XXXXXX whose process PID
    // no longer runs.  A name of the prefix that is not of this form stays,
    // and so does the directory of this process and of each live process.
    // Complexity: linear in the number of entries of kDirectory.
    static void remove_directories_of_ended_processes(std::string_view prefix) {
        const std::string lead = std::string{prefix} + "-";
        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{std::filesystem::path{kDirectory}, error}) {
            const std::string name = entry.path().filename().string();
            if (!std::string_view{name}.starts_with(lead)) continue;
            const std::string_view rest = std::string_view{name}.substr(lead.size());
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

private:
    std::string path_;
};

// The cache root of one test process.  A store finds its root through
// XDG_CACHE_HOME, so a test that commits sets that variable to a fresh
// scratch directory, and the store never writes to the real cache of the
// machine.  The destructor puts back the value that the variable had before,
// and the scratch directory then removes itself.  The process stops when the
// directory or the variable cannot be set.
class ScopedCacheHome final {
public:
    explicit ScopedCacheHome(std::string_view prefix) : scratch_{prefix} {
        if (const char* earlier = std::getenv("XDG_CACHE_HOME"); earlier != nullptr) {
            had_earlier_ = true;
            earlier_ = earlier;
        }
        if (!scratch_.is_ready() || ::setenv("XDG_CACHE_HOME", scratch_.path().c_str(), 1) != 0) {
            std::abort();
        }
    }

    ~ScopedCacheHome() {
        if (had_earlier_) {
            (void)::setenv("XDG_CACHE_HOME", earlier_.c_str(), 1);
        } else {
            (void)::unsetenv("XDG_CACHE_HOME");
        }
    }

    ScopedCacheHome(const ScopedCacheHome&) = delete("one cache root has one owner");
    ScopedCacheHome& operator=(const ScopedCacheHome&) = delete("one cache root has one owner");

    [[nodiscard]] const std::string& path() const noexcept { return scratch_.path(); }

private:
    ScratchDir scratch_;
    std::string earlier_;
    bool had_earlier_ = false;
};

}  // namespace crucible::test
