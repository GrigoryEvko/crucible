#include <fixy/os/NumaPlacement.h>

namespace fixy::detail {

namespace {

// The pages of one query.  The kernel walks a query in chunks of 16
// pages, so a larger batch saves only system calls, and 512 pages keep
// the two arrays of a batch at 6 KB of stack.
constexpr std::size_t census_batch_pages = 512;

}  // namespace

// move_pages(2) with no target nodes moves nothing, and it writes the node
// of each page, or a negative errno for a page with no node, into the
// status array.  The addresses go through integers, because the first page
// of a range that is not page-aligned starts before the range, and a
// pointer to it is outside the object of the range.  The kernel reads the
// addresses and the program does not dereference them.
int census_page_nodes_(void const* first, std::size_t bytes, numa::PageNodeCensus& census) noexcept {
    if (bytes == 0) {
        return 0;
    }
    long const page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return EINVAL;
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
        long const queried = ::syscall(
            SYS_move_pages, 0, count, pages, nullptr, statuses,
            0);  // SYSCALL-CAP-OK: detail helper for census_page_nodes ctx-gate (CtxFitsNumaQuery: effects::IO+Block)
        if (queried != 0) {
            // A query moves nothing, so a count of pages that did not move
            // is not a possible answer.  It refuses the census as EIO.
            return queried < 0 && errno != 0 ? errno : EIO;
        }
        for (std::size_t index = 0; index < count; ++index) {
            census.count_page_(statuses[index]);
        }
        done += count;
    }
    return 0;
}

}  // namespace fixy::detail
