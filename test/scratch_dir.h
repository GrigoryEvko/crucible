#pragma once

// The scratch directory of a test process, and the cache root of a test that
// commits to a store.
//
// make_scratch_directory sweeps the trees that ended processes left under the
// temporary directory of the host (the sweep of fixy/os/Fs.h), and then makes
// a temporary directory of the base (the temporary directory).  The end of the
// owner removes its tree.  An abort runs no end, so a test that aborts leaves
// its tree only until the sweep of a later test.  The sweep keeps each tree of
// a live process.  make_scratch_path gives the path of an entry under the
// scratch directory.
//
// ScopedCacheHome sets XDG_CACHE_HOME to a scratch directory through the C
// library, because the environment door of the base reads the environment one
// time at the start and has no set door.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdlib>
#include <utility>

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

// The cache root of one test process.  A store finds its root through
// XDG_CACHE_HOME, so a test that commits sets that variable to a scratch
// directory, and the store never writes to the real cache of the machine.  The
// end puts back the value that the variable had before, and the scratch
// directory then removes its tree.  The process stops when the directory or
// the variable cannot be set, and when the value before holds more characters
// than a path.
class ScopedCacheHome final {
public:
    template <::foundation::effects::IsExecCtx Ctx>
        requires ::fixy::fs::CtxFitsTemporaryDirectoryMint<Ctx>
    explicit ScopedCacheHome(Ctx const& ctx) noexcept
        : scratch_{make_scratch_directory(ctx).expect("the cache root makes its scratch directory")} {
        if (char const* const earlier = ::getenv("XDG_CACHE_HOME"); earlier != nullptr) {
            ScratchPath value{};
            for (std::size_t index = 0; earlier[index] != '\0'; ++index) {
                value.append(earlier[index]).expect("the value of XDG_CACHE_HOME before fits a path");
            }
            earlier_ = ::fixy::Option<ScratchPath>::some(value);
        }
        if (::setenv("XDG_CACHE_HOME", ::foundation::core::detail::c_text_first_(scratch_.path()), 1) != 0) {
            ::fixy::fatal("the cache root sets XDG_CACHE_HOME");
        }
    }

    ~ScopedCacheHome() {
        earlier_.match(
            [](ScratchPath const& value) noexcept {
                static_cast<void>(
                    ::setenv("XDG_CACHE_HOME", ::foundation::core::detail::c_text_first_(value.c_text()), 1));
            },
            [] noexcept { static_cast<void>(::unsetenv("XDG_CACHE_HOME")); });
    }

    ScopedCacheHome(ScopedCacheHome const&) = delete("one cache root has one owner");
    ScopedCacheHome& operator=(ScopedCacheHome const&) = delete("one cache root has one owner");

    // The path of the scratch directory, the value of XDG_CACHE_HOME.
    [[nodiscard]] ::fixy::CText path() const& noexcept { return scratch_.path(); }
    void path() const&& = delete("the path of a temporary cache root dangles at the end of the full expression");

private:
    ::fixy::fs::TemporaryDirectory scratch_{};
    // The value of XDG_CACHE_HOME before, or no value when the variable was
    // absent.
    ::fixy::Option<ScratchPath> earlier_{};
};

}  // namespace crucible::test
