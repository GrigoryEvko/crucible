// The page door of fixy (fixy/os/Pages.h): the lock, the unlock, the fault in
// and the huge page advice of a range, and the THP setting of the process.  It
// issues the raw primitive of foundation/os/Syscall.h, and
// foundation/os/MapWords.h gives the words of the memory calls, so this file
// includes no system header.

#include <fixy/os/Pages.h>

#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::pages::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

using UnitResult = core::Result<core::Unit, os::Errno>;

// The options of prctl for the THP setting, PR_SET_THP_DISABLE and
// PR_GET_THP_DISABLE.  <linux/prctl.h> gives them, and no door owns that
// header.  The test of the page door sets the setting and reads it back, so
// a wrong value fails it.
constexpr int kSetThpDisable = 41;
constexpr int kGetThpDisable = 42;

// The bit of the THP word that refuses huge pages.  The bits above it are
// the flags of the refusal, which PR_SET_THP_DISABLE takes as its third
// argument.
constexpr std::uint32_t kRefuseBit = 1;

}  // namespace

UnitResult mlock_sys(PageRange range) noexcept {
    return os::unit_of(os::raw_syscall(os::Syscall::mlock, std::to_underlying(range.first_), range.bytes_));
}

UnitResult mlock2_sys(PageRange range, unsigned flags) noexcept {
    return os::unit_of(os::raw_syscall(os::Syscall::mlock2, std::to_underlying(range.first_), range.bytes_, flags));
}

UnitResult munlock_sys(PageRange range) noexcept {
    return os::unit_of(os::raw_syscall(os::Syscall::munlock, std::to_underlying(range.first_), range.bytes_));
}

// The first huge page boundary at or after the start of the range, and the
// count of whole huge pages from it.  A start in the last huge page of the
// address space has no such boundary, and the range then has no whole huge
// page.
core::Result<std::size_t, os::Errno> madvise_sys(PageRange range, int advice) noexcept {
    constexpr std::uintptr_t kMask = huge_page_bytes - 1;
    std::uintptr_t const start = std::to_underlying(range.first_);
    std::size_t const bytes = range.bytes_;
    CRUCIBLE_LET_ELSE(aligned, core::align_up(start, std::uintptr_t{huge_page_bytes}), std::size_t{0});
    std::size_t const head = aligned - start;
    if (head >= bytes) {
        return std::size_t{0};
    }
    std::size_t const whole = (bytes - head) & ~kMask;
    if (whole == 0) {
        return std::size_t{0};
    }
    return os::result_of(os::raw_syscall(os::Syscall::madvise, aligned, whole, advice))
        .map([whole](std::uint64_t) noexcept { return whole; });
}

// The kernel refuses a start that is not aligned to a page, so the start goes
// down to the first byte of its page.  The kernel rounds the length up to a
// whole page, so the length runs to the end of the range and no further.  The
// size of a page comes from the auxiliary vector, because an aarch64 kernel
// can use pages of 16 KiB or 64 KiB.  A vector that the door cannot read
// gives einval, as the census of fixy/os/NumaPlacement.h does.
UnitResult populate_sys(PageRange range) noexcept {
    std::size_t const bytes = range.bytes_;
    if (bytes == 0) {
        return core::Unit{};
    }
    std::uint64_t const page_size = os::read_auxiliary(os::AuxiliaryEntry::page_size).value_or(0);
    if (page_size == 0) {
        return core::err(os::Errno::einval);
    }
    std::uintptr_t const start = std::to_underlying(range.first_);
    std::uintptr_t const page_start = start - (start % static_cast<std::uintptr_t>(page_size));
    std::size_t const length = (start - page_start) + bytes;
    UnitResult const advised =
        os::unit_of(os::raw_syscall(os::Syscall::madvise, page_start, length, os::kAdvisePopulateWrite));
    if (advised.is_err()) {
        return advised;
    }
    // A kernel that takes the advice faults in each page before it returns.
    // qemu user mode takes the advice and faults in no page, so the door reads
    // the residency of the first page (mincore) and gives eopnotsupp when the
    // page has no frame.  The low bit of the byte of a page is its residency.
    unsigned char residency = 0;
    UnitResult const queried = os::unit_of(os::raw_syscall(os::Syscall::mincore, page_start, page_size, &residency));
    if (queried.is_err()) {
        return queried;
    }
    if ((residency & 1U) == 0) {
        return core::err(os::Errno::eopnotsupp);
    }
    return core::Unit{};
}

// The kernel takes the refusal as the second argument and its flags as the
// third, and an older kernel refuses a third argument that is not zero.  A
// setting of zero clears the refusal with no flags.
core::Result<ThpSetting, os::Errno> prctl_sys(ThpSetting next) noexcept {
    CRUCIBLE_TRY(prior, os::result_of(os::raw_syscall(os::Syscall::prctl, kGetThpDisable, 0, 0, 0, 0)));
    auto const prior_word = static_cast<std::uint32_t>(prior);
    std::uint32_t const word = std::to_underlying(next);
    std::uint32_t const refuse = word != 0 ? kRefuseBit : 0;
    std::uint32_t const flags = word & ~kRefuseBit;
    return os::unit_of(os::raw_syscall(os::Syscall::prctl, kSetThpDisable, refuse, flags, 0, 0))
        .map([prior_word](core::Unit) noexcept { return static_cast<ThpSetting>(prior_word); });
}

}  // namespace fixy::pages::detail
