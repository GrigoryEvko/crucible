// The page door of fixy (fixy/os/Pages.h): the lock, the unlock and the huge
// page advice of a range, and the THP setting of the process.  It issues the
// raw primitive of foundation/os/Syscall.h, and foundation/os/MapWords.h gives
// the words of the memory calls, so this file includes no system header.

#include <fixy/os/Pages.h>

#include <foundation/core/Choice.h>
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

UnitResult mlock_sys(void const* first, std::size_t bytes) noexcept {
    return os::unit_of(os::raw_syscall(os::Syscall::mlock, first, bytes));
}

UnitResult mlock2_sys(void const* first, std::size_t bytes, unsigned flags) noexcept {
    return os::unit_of(os::raw_syscall(os::Syscall::mlock2, first, bytes, flags));
}

UnitResult munlock_sys(void const* first, std::size_t bytes) noexcept {
    return os::unit_of(os::raw_syscall(os::Syscall::munlock, first, bytes));
}

// The first huge page boundary at or after the start of the range, and the
// count of whole huge pages from it.  A start in the last huge page of the
// address space has no such boundary, and the range then has no whole huge
// page.
core::Result<std::size_t, os::Errno> madvise_sys(void const* first, std::size_t bytes, int advice) noexcept {
    using Advised = core::Result<std::size_t, os::Errno>;
    constexpr std::uintptr_t kMask = huge_page_bytes - 1;
    std::uintptr_t const start = __builtin_bit_cast(std::uintptr_t, first);
    if (start > ~std::uintptr_t{0} - kMask) {
        return std::size_t{0};
    }
    std::uintptr_t const aligned = (start + kMask) & ~kMask;
    std::size_t const head = aligned - start;
    if (head >= bytes) {
        return std::size_t{0};
    }
    std::size_t const whole = (bytes - head) & ~kMask;
    if (whole == 0) {
        return std::size_t{0};
    }
    return os::result_of(os::raw_syscall(os::Syscall::madvise, aligned, whole, advice))
        .match([whole](std::uint64_t) -> Advised { return whole; },
               [](os::Errno error) -> Advised { return core::err(error); });
}

// The kernel takes the refusal as the second argument and its flags as the
// third, and an older kernel refuses a third argument that is not zero.  A
// setting of zero clears the refusal with no flags.
core::Result<ThpSetting, os::Errno> prctl_sys(ThpSetting next) noexcept {
    using Exchanged = core::Result<ThpSetting, os::Errno>;
    core::Result<std::uint64_t, os::Errno> prior =
        os::result_of(os::raw_syscall(os::Syscall::prctl, kGetThpDisable, 0, 0, 0, 0));
    for (os::Errno const error : prior.err()) {
        return core::err(error);
    }
    auto const prior_word = static_cast<std::uint32_t>(std::move(prior).expect("the read succeeded above"));
    std::uint32_t const word = std::to_underlying(next);
    std::uint32_t const refuse = word != 0 ? kRefuseBit : 0;
    std::uint32_t const flags = word & ~kRefuseBit;
    return os::unit_of(os::raw_syscall(os::Syscall::prctl, kSetThpDisable, refuse, flags, 0, 0))
        .match([prior_word](core::Unit) -> Exchanged { return static_cast<ThpSetting>(prior_word); },
               [](os::Errno error) -> Exchanged { return core::err(error); });
}

}  // namespace fixy::pages::detail
