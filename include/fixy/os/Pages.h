#pragma once

// Page doors
//   The lock, the unlock, the fault in and the huge page advice of the pages
//   of a range of memory, and the setting of transparent huge pages (THP) of
//   the process.
//   Each call takes its range as a PageRange, and storage_of gives the range
//   of an object or of a run of elements.  The door file
//   src/fixy/os/Pages.cpp issues each call with the raw primitive of
//   foundation/os/Syscall.h, and gives the error of the kernel in a Result.
//   A function that issues one system call name out of line has the name
//   name_sys (the affinity door of fixy/os/CpuPinned.h).
//
// Page range
//   A PageRange holds the address of the first byte of a range as a word, and
//   the count of its bytes.  It gives out no pointer, no View and no Ref, so
//   no code reads or writes memory through it.  Only the system calls of the
//   page doors read it: they change the residency, the lock and the page size
//   of the pages, and no byte of them.  So a PageRange is no borrow, and it can
//   outlive the owner of its memory.  A stale range then causes no fault: the
//   kernel refuses a range with no mapping (enomem), and a range that a later
//   owner maps again gets a change of residency, which changes no byte of
//   that owner.  Only storage_of makes a range, from an object or a run that
//   the caller holds, so no range comes from a raw address.
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
#include <foundation/ByteSeal.h>
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
#include <type_traits>

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

// The row of the page calls of this header: mlock, mlock2, munlock and
// madvise.  Each is a call of the mapping family, so each lifts IO and
// Block.  The family atom of the mappings also names each call of the
// families below it on the chain, a read of the clock among them, and it
// lifts their Nondet, which no page call has.
using page_call_row_t = eff::row_union_t<
    eff::row_union_t<eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::mlock>>,
                     eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::mlock2>>>,
    eff::row_union_t<eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::munlock>>,
                     eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::madvise>>>>;

template <typename Ctx>
concept CtxFitsPageCall = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, page_call_row_t>;

using thp_call_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::prctl>>;

template <typename Ctx>
concept CtxFitsThpCall =
    eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, thp_call_row_t> && eff::CtxOwnsCapability<Ctx, eff::Effect::Init>;

// The bits of the address of the first byte of a page range.  The word names
// memory of an owner, and no object of this process: only the system calls
// of the page doors read it (page range).
enum class PageWord : std::uintptr_t {
};

class PageRange;

namespace detail {

// Locks each page that holds a byte of range, and faults in each page that is
// not in memory.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mlock_sys(PageRange range) noexcept;

// Locks each page that holds a byte of range, with the flags of mlock2.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mlock2_sys(PageRange range, unsigned flags) noexcept;

// Unlocks each page that holds a byte of range.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
munlock_sys(PageRange range) noexcept;

// Gives the given advice for each whole huge page of range, and gives the
// count of the bytes of those huge pages.  The range is rounded inward to
// huge_page_bytes, because the kernel refuses an address that is not aligned
// to a huge page.  A range with no whole huge page gives zero and makes no
// system call.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno> madvise_sys(PageRange range,
                                                                                           int advice) noexcept;

// Faults in for a write each page that holds a byte of range
// (MADV_POPULATE_WRITE).  The range is rounded outward to whole pages, with
// the size of a page from the auxiliary vector.  A count of zero makes no
// system call.  After the advice, mincore reads the residency of the first
// page, and a page with no frame gives eopnotsupp.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
populate_sys(PageRange range) noexcept;

// Reads the THP setting of the process, then sets it to next, and gives the
// setting of the read.  A refused set gives its error and changes nothing.
[[nodiscard]] ::foundation::core::Result<ThpSetting, ::foundation::os::Errno> prctl_sys(ThpSetting next) noexcept;

}  // namespace detail

template <class T>
    requires(std::is_object_v<T> && !std::is_array_v<T>)
[[nodiscard]] PageRange storage_of(T const& object) noexcept;

template <class T, std::size_t Extent>
[[nodiscard]] PageRange storage_of(::foundation::core::View<T, Extent> run) noexcept;

// The address word of the first byte of a range of the memory of an owner,
// and the count of its bytes (page range).  A range copies, and its copy
// names the same pages.  The empty range starts at no byte.
class [[nodiscard]] PageRange final {
public:
    // The empty range, which makes no system call at a page door.
    constexpr PageRange() noexcept = default;

    // The count of the bytes of the range.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return bytes_; }

    // True when the two ranges start at one byte.  An empty range starts at no
    // byte.  The two ranges can swap with no change of the answer.
    [[nodiscard]] constexpr bool has_same_start(PageRange const& other) const noexcept {
        return bytes_ != 0 && other.bytes_ != 0 && first_ == other.first_;
    }

private:
    template <class T>
        requires(std::is_object_v<T> && !std::is_array_v<T>)
    friend PageRange storage_of(T const& object) noexcept;
    template <class T, std::size_t Extent>
    friend PageRange storage_of(::foundation::core::View<T, Extent> run) noexcept;

    friend auto detail::mlock_sys(PageRange range) noexcept
        -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>;
    friend auto detail::mlock2_sys(PageRange range, unsigned flags) noexcept
        -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>;
    friend auto detail::munlock_sys(PageRange range) noexcept
        -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>;
    friend auto detail::madvise_sys(PageRange range, int advice) noexcept
        -> ::foundation::core::Result<std::size_t, ::foundation::os::Errno>;
    friend auto detail::populate_sys(PageRange range) noexcept
        -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>;

    // The range of the bytes of the storage of an object or a run, which only
    // storage_of gives.
    explicit PageRange(::foundation::core::View<std::byte const> bytes) noexcept
        : first_{static_cast<PageWord>(
              __builtin_bit_cast(std::uintptr_t, first_of_(::foundation::core::detail::BaseLayerRead{}, bytes)))},
          bytes_{bytes.size()} {}

    PageWord first_{};
    std::size_t bytes_ = 0;
    // A copy of bytes builds no range, so each range comes from storage_of
    // (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The range of the storage of object: the sizeof(T) bytes that hold it.  An
// owner gives the pages of an object to a lock or a fault in.  The range of a
// temporary is refused, because its storage ends at the end of the full
// expression.  A hash or a wire takes bytes() of foundation/core/Algorithms.h,
// which reads the bytes, and no range.  Complexity: O(1).
template <class T>
    requires(std::is_object_v<T> && !std::is_array_v<T>)
[[nodiscard]] PageRange storage_of(T const& object) noexcept {
    return PageRange{::foundation::core::View<std::byte const>{::foundation::core::detail::storage_of_object_(object)}};
}

template <class T>
    requires(std::is_object_v<T> && !std::is_array_v<T>)
void storage_of(T const&&) = delete("the storage of a temporary object ends at the end of the full expression");

// The range of the storage of the elements of run: size() * sizeof(T) bytes.
// A View holds at most __PTRDIFF_MAX__ bytes, so the count of bytes does not
// wrap.  Complexity: O(1).
template <class T, std::size_t Extent>
[[nodiscard]] PageRange storage_of(::foundation::core::View<T, Extent> run) noexcept {
    return PageRange{::foundation::core::detail::storage_of_run_(run)};
}

// Locks the pages of range in memory, and faults in each page that is not in
// memory.  The limit RLIMIT_MEMLOCK of an unprivileged process gives enomem
// for a range past it.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
lock_pages(Ctx const&, PageRange range) noexcept {
    return detail::mlock_sys(range);
}

// Locks the pages of range in memory when each page faults in, and faults in
// no page at the call (mlock2 with MLOCK_ONFAULT).  An older kernel than 4.4
// gives enosys.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
lock_pages_on_fault(Ctx const&, PageRange range) noexcept {
    return detail::mlock2_sys(range, ::foundation::os::kLockOnFault);
}

// Unlocks the pages of range.  A page that no lock holds stays as it is.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
unlock_pages(Ctx const&, PageRange range) noexcept {
    return detail::munlock_sys(range);
}

// Faults in each page that holds a byte of range, as a write does, and
// writes no byte (MADV_POPULATE_WRITE).  Each page then has its own frame and
// a page table entry that permits a write, so the first write of the owner
// takes no fault.  A thread that writes the range at the same time loses no
// write, because the kernel changes no byte.  A range that the process cannot
// write gives einval or efault, by the release of the kernel.  An older kernel
// than 5.14 gives einval.  A kernel that takes the advice and faults in no
// page, as qemu user mode does, gives eopnotsupp, so the door claims no
// populate that did not occur.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
populate_pages(Ctx const&, PageRange range) noexcept {
    return detail::populate_sys(range);
}

// Asks the kernel for huge pages in each whole huge page of range
// (MADV_HUGEPAGE), and gives the count of the advised bytes.  The advice
// changes no byte of the range.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
advise_huge_pages(Ctx const&, PageRange range) noexcept {
    return detail::madvise_sys(range, ::foundation::os::kAdviseHugePage);
}

// Makes huge pages of each whole huge page of range before the call returns
// (MADV_COLLAPSE), and gives the count of the collapsed bytes.  An older
// kernel than 6.1 gives einval.  The collapse changes no byte of the range.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPageCall<Ctx>
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
collapse_huge_pages(Ctx const&, PageRange range) noexcept {
    return detail::madvise_sys(range, ::foundation::os::kAdviseCollapse);
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
