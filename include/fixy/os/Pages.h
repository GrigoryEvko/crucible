#pragma once

// THE PAGE DOORS
//   The lock, the unlock and the huge page advice of the pages of a range of
//   memory, and the setting of transparent huge pages (THP) of the process.
//   Each call takes its range as a View of bytes.  The door file
//   src/fixy/os/Pages.cpp issues each call with the raw primitive of
//   foundation/os/Syscall.h, and gives the error of the kernel in a Result.
//   A function that issues one system call NAME out of line has the name
//   NAME_sys (THE AFFINITY DOOR of fixy/os/CpuPinned.h).
//
//   The kernel keeps one lock bit for each page, and not one lock for each
//   call.  An unlock of a range unlocks each page of the range, also a page
//   that a lock of a different range holds.
//
//   The gate of the page calls is the row of the memory mapping family of
//   the syscall catalog, IO and Block: a lock can fault in each page of its
//   range.  The THP setting is a state of the whole process, so its gate
//   also needs Init.

#include <fixy/atoms/Syscall.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MapWords.h>

#include <cstddef>
#include <cstdint>

namespace fixy::pages {

namespace eff = ::foundation::effects;

// The size of a huge page of THP on x86_64, and on aarch64 with pages of
// 4 KiB.
inline constexpr std::size_t huge_page_bytes = std::size_t{2} * 1024 * 1024;

// The THP setting of the process: the word of PR_GET_THP_DISABLE.  Zero lets
// the kernel give huge pages to the process.  Bit 0 refuses them, and the
// bits above bit 0 are the flags of the refusal.  A word with flags is a
// value of the enumeration too.
enum class ThpSetting : std::uint32_t {
    allowed = 0,
    refused = 1,
};

using page_call_row_t =
    eff::lift_row_t<::fixy::atom::syscall::family<::fixy::atom::syscall::SyscallFamily::MemoryMapping>>;

template <typename Ctx>
concept CtxFitsPageCall = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, page_call_row_t>;

using thp_call_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::prctl>>;

template <typename Ctx>
concept CtxFitsThpCall =
    eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, thp_call_row_t> && eff::CtxOwnsCapability<Ctx, eff::Effect::Init>;

namespace detail {

// Locks each page that holds a byte of the BYTES bytes at FIRST, and faults
// in each page that is not in memory.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mlock_sys(void const* first, std::size_t bytes) noexcept;

// Locks each page that holds a byte of the BYTES bytes at FIRST, with the
// flags FLAGS of mlock2.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mlock2_sys(void const* first, std::size_t bytes, unsigned flags) noexcept;

// Unlocks each page that holds a byte of the BYTES bytes at FIRST.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
munlock_sys(void const* first, std::size_t bytes) noexcept;

// Gives the advice ADVICE for each whole huge page in the BYTES bytes at
// FIRST, and gives the count of the bytes of those huge pages.  The range is
// rounded inward to huge_page_bytes, because the kernel refuses an address
// that is not aligned to a huge page.  A range with no whole huge page gives
// zero and makes no system call.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
madvise_sys(void const* first, std::size_t bytes, int advice) noexcept;

// Reads the THP setting of the process, then sets it to NEXT, and gives the
// setting of the read.  A refused set gives its error and changes nothing.
[[nodiscard]] ::foundation::core::Result<ThpSetting, ::foundation::os::Errno> prctl_sys(ThpSetting next) noexcept;

}  // namespace detail

// Locks the pages of bytes in memory, and faults in each page that is not in
// memory.  The limit RLIMIT_MEMLOCK of an unprivileged process gives enomem
// for a range past it.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
lock_pages(Ctx const&, ::foundation::core::View<std::byte const> bytes) noexcept {
    return detail::mlock_sys(first_of_(::foundation::core::detail::ViewDoor::key(), bytes), bytes.size());
}

// Locks the pages of bytes in memory when each page faults in, and faults in
// no page at the call (mlock2 with MLOCK_ONFAULT).  An older kernel than 4.4
// gives enosys.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
lock_pages_on_fault(Ctx const&, ::foundation::core::View<std::byte const> bytes) noexcept {
    return detail::mlock2_sys(first_of_(::foundation::core::detail::ViewDoor::key(), bytes), bytes.size(),
                              ::foundation::os::kLockOnFault);
}

// Unlocks the pages of bytes.  A page that no lock holds stays as it is.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
unlock_pages(Ctx const&, ::foundation::core::View<std::byte const> bytes) noexcept {
    return detail::munlock_sys(first_of_(::foundation::core::detail::ViewDoor::key(), bytes), bytes.size());
}

// Asks the kernel for huge pages in each whole huge page of bytes
// (MADV_HUGEPAGE), and gives the count of the advised bytes.  The advice
// changes no byte of the range.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
advise_huge_pages(Ctx const&, ::foundation::core::View<std::byte const> bytes) noexcept {
    return detail::madvise_sys(first_of_(::foundation::core::detail::ViewDoor::key(), bytes), bytes.size(),
                               ::foundation::os::kAdviseHugePage);
}

// Makes huge pages of each whole huge page of bytes before the call returns
// (MADV_COLLAPSE), and gives the count of the collapsed bytes.  An older
// kernel than 6.1 gives einval.  The collapse changes no byte of the range.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
collapse_huge_pages(Ctx const&, ::foundation::core::View<std::byte const> bytes) noexcept {
    return detail::madvise_sys(first_of_(::foundation::core::detail::ViewDoor::key(), bytes), bytes.size(),
                               ::foundation::os::kAdviseCollapse);
}

// Sets the THP setting of the process to next, and gives the setting before
// the call.  A second call with the given setting puts the process back.  A
// child process takes the setting of its parent.  The read and the set are
// two calls, so two threads that change the setting at the same time can
// lose a change.
template <eff::IsExecCtx Ctx>
    requires CtxFitsThpCall<Ctx>
[[nodiscard]] ::foundation::core::Result<ThpSetting, ::foundation::os::Errno>
exchange_thp_setting(Ctx const&, ThpSetting next) noexcept {
    return detail::prctl_sys(next);
}

}  // namespace fixy::pages
