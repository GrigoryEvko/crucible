#pragma once

// The exit hook door of fixy: a function that the process runs at
// std::exit and at std::quick_exit.
//
// run_at_exit registers the hook with atexit and with at_quick_exit of the
// C library (decision D12 of misc/01_10_2026_quarantine.md).  The process
// runs the hooks in the reverse order of their registration.  std::abort,
// std::_Exit and a signal run no hook.  A hook changes the process for the
// rest of its life, so the gate asks for a context that owns Effect::Init.
//
// This header is in the layer X3_os of utils/rule-table.txt.  The door file
// is src/fixy/os/ExitHooks.cpp, the one includer of <stdlib.h>.

#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

namespace fixy::exit {

// A function that the process runs at its exit.
using ExitHook = void (*)() noexcept;

namespace detail {

[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_at_exit_(ExitHook hook) noexcept;

}  // namespace detail

// A context that can register an exit hook: one that owns Effect::Init.
template <class Ctx>
concept CtxRegistersExitHooks = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// Registers HOOK for std::exit and for std::quick_exit.  The C library gives
// no error number, so a refused registration gives Errno::enomem, the one
// cause of a refusal.  A refusal of the second registration keeps the first.
template <class Ctx>
    requires CtxRegistersExitHooks<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
run_at_exit(Ctx const& /*init*/, ExitHook hook) noexcept {
    return detail::run_at_exit_(hook);
}

}  // namespace fixy::exit
