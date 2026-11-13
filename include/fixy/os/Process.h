#pragma once

// The identity door of fixy: the id of the calling process and the id of the
// calling thread, as the kernel gives them.
//
// The door file src/fixy/os/Process.cpp issues getpid and gettid with the raw
// primitive of foundation/os/Syscall.h, so this header includes no system
// header.  The two calls cannot fail.  Each read takes a context that admits
// the row of the family of the two calls in the syscall catalog, the family
// of the reads of the process state.

#include <fixy/atoms/Syscall.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <cstdint>

namespace fixy::process {

namespace eff = ::foundation::effects;

// The id of a process: the id of its thread group, which getpid gives.
enum class ProcessId : std::uint32_t {
};

// The id of a thread, which gettid gives.  The id of the first thread of a
// process is the id of the process.
enum class ThreadId : std::uint32_t {
};

namespace detail {

// The two calls of the door file.
[[nodiscard]] ProcessId current_process_() noexcept;
[[nodiscard]] ThreadId current_thread_() noexcept;

}  // namespace detail

// The row comes from the syscall atom, so the gate and the catalog in
// fixy/atoms/Syscall.h cannot disagree.  The catalog puts getpid and gettid
// in the family of the reads of the process state, which lifts to IO.
using identity_row_t =
    eff::lift_row_t<::fixy::atom::syscall::family<::fixy::atom::syscall::SyscallFamily::ReadOnlyState>>;

template <typename Ctx>
concept CtxFitsIdentityRead = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, identity_row_t>;

// The id of the calling process.
template <eff::IsExecCtx Ctx>
    requires CtxFitsIdentityRead<Ctx>
[[nodiscard]] inline ProcessId current_process(Ctx const&) noexcept {
    return detail::current_process_();
}

// The id of the calling thread.
template <eff::IsExecCtx Ctx>
    requires CtxFitsIdentityRead<Ctx>
[[nodiscard]] inline ThreadId current_thread(Ctx const&) noexcept {
    return detail::current_thread_();
}

}  // namespace fixy::process
