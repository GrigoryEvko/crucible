// The memory policy door of fixy/os/NumaPlacement.h: the binding of a range
// to one node and the census of the nodes of the pages of a range, through
// the raw system call primitive of foundation/os/Syscall.h.
//
// The argument words come from foundation/os/MemoryPolicyWords.h, which the
// door file of the primitive compares with the kernel header, and the size of
// a page comes from the auxiliary vector door.  So this file includes no
// header of the kernel and no header of the C library.

#include <fixy/os/NumaPlacement.h>

#include <foundation/core/Choice.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MemoryPolicyWords.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace fixy::detail {

namespace {

// The pages of one query.  The kernel walks a query in chunks of 16
// pages, so a larger batch saves only system calls, and 512 pages keep
// the two arrays of a batch at 6 KB of stack.
constexpr std::size_t census_batch_pages = 512;

// The process id that names the calling process in a query of move_pages.
constexpr int kCallingProcess = 0;

// The flags of a query of move_pages: none.
constexpr int kNoFlags = 0;

// The result of one query of move_pages.  A query moves nothing, so a count
// of pages that did not move is not a possible answer, and the door refuses
// the census as EIO.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
query_of(long answer) noexcept {
    return ::foundation::os::result_of(answer).match(
        [](std::uint64_t not_moved) noexcept
            -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> {
            if (not_moved != 0) {
                return ::foundation::core::err(::foundation::os::Errno::eio);
            }
            return ::foundation::core::Unit{};
        },
        [](::foundation::os::Errno error) noexcept
            -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> {
            return ::foundation::core::err(error);
        });
}

}  // namespace

// The kernel decrements the node count that it receives before it reads
// the mask, so the call passes one more than the width of the mask.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mbind_sys(void* address, std::size_t length, NumaNodeId node) noexcept {
    unsigned long mask[numa_mask_bits / numa_mask_word_bits]{};
    std::size_t const bit = static_cast<std::size_t>(node);
    mask[bit / numa_mask_word_bits] = 1UL << (bit % numa_mask_word_bits);
    return ::foundation::os::unit_of(::foundation::os::raw_syscall(
        ::foundation::os::Syscall::mbind, address, length, ::foundation::os::kMemoryPolicyBind,
        static_cast<void const*>(mask), numa_mask_bits + 1,
        ::foundation::os::kMemoryPolicyStrict | ::foundation::os::kMemoryPolicyMove));
}

// move_pages(2) with no target nodes moves nothing, and it writes the node
// of each page, or a negative errno for a page with no node, into the
// status array.  The addresses go through integers, because the first page
// of a range that is not page-aligned starts before the range, and a
// pointer to it is outside the object of the range.  The kernel reads the
// addresses and the program does not dereference them.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
census_page_nodes_(void const* first, std::size_t bytes, numa::PageNodeCensus& census) noexcept {
    if (bytes == 0) {
        return ::foundation::core::Unit{};
    }
    std::uint64_t const page_size =
        ::foundation::os::read_auxiliary(::foundation::os::AuxiliaryEntry::page_size).value_or(0);
    if (page_size == 0) {
        return ::foundation::core::err(::foundation::os::Errno::einval);
    }
    std::uintptr_t const page_bytes = static_cast<std::uintptr_t>(page_size);
    std::uintptr_t const start = __builtin_bit_cast(std::uintptr_t, first);
    std::uintptr_t const last = start + (bytes - 1);
    std::uintptr_t const first_page = start - (start % page_bytes);
    std::size_t const page_count = ((last - (last % page_bytes) - first_page) / page_bytes) + 1;

    void const* pages[census_batch_pages]{};
    int statuses[census_batch_pages]{};
    for (std::size_t done = 0; done < page_count;) {
        std::size_t const remaining = page_count - done;
        std::size_t const count = remaining < census_batch_pages ? remaining : census_batch_pages;
        for (std::size_t index = 0; index < count; ++index) {
            pages[index] = __builtin_bit_cast(void const*, first_page + ((done + index) * page_bytes));
        }
        ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> const queried =
            query_of(::foundation::os::raw_syscall(::foundation::os::Syscall::move_pages, kCallingProcess, count,
                                                   static_cast<void const*>(pages), ::foundation::os::Word{},
                                                   static_cast<void const*>(statuses), kNoFlags));
        if (queried.is_err()) {
            return queried;
        }
        for (std::size_t index = 0; index < count; ++index) {
            census.count_page_(statuses[index]);
        }
        done += count;
    }
    return ::foundation::core::Unit{};
}

}  // namespace fixy::detail
