// The auxiliary vector door of the base (foundation/os/Auxiliary.h).  It is
// the one includer of <linux/auxvec.h>, and it reads the vector through the
// file door.

#include <foundation/os/Auxiliary.h>

#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>
#include <foundation/os/File.h>

#include <linux/auxvec.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

namespace {

// The bytes of the vector that the door reads.  The kernel gives fewer than
// 64 entries of 16 bytes.
constexpr std::size_t kAuxvCapacity = 4096;

// The type of the vector entry of each enumerator.
[[nodiscard]] constexpr std::uint64_t type_of(AuxiliaryEntry entry) noexcept {
    switch (entry) {
        case AuxiliaryEntry::page_size:
            return AT_PAGESZ;
        case AuxiliaryEntry::minimum_signal_stack:
            return AT_MINSIGSTKSZ;
        default:
            return AT_NULL;
    }
}

}  // namespace

core::Option<std::uint64_t> read_auxiliary(AuxiliaryEntry entry) noexcept {
    std::uint64_t const wanted = type_of(entry);
    if (wanted == AT_NULL) return core::Option<std::uint64_t>{core::none};
    core::FixedText<kAuxvCapacity> auxiliary{};
    if (read_file("/proc/self/auxv", auxiliary).is_err()) return core::Option<std::uint64_t>{core::none};
    char const* const bytes = core::detail::text_first_(auxiliary.view());
    constexpr std::size_t kEntryBytes = 2 * sizeof(std::uint64_t);
    for (std::size_t offset = 0; offset + kEntryBytes <= auxiliary.size(); offset += kEntryBytes) {
        std::uint64_t type = 0;
        std::uint64_t value = 0;
        __builtin_memcpy(&type, bytes + offset, sizeof type);
        __builtin_memcpy(&value, bytes + offset + sizeof type, sizeof value);
        if (type == AT_NULL) break;
        if (type == wanted && value > 0) return core::Option<std::uint64_t>::some(value);
    }
    return core::Option<std::uint64_t>{core::none};
}

}  // namespace foundation::os
