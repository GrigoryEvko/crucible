// The memory policy door of fixy/os/NumaPlacement.h: the policy of a range,
// the census of the nodes of the pages of a range, and the policy of the
// calling thread, through the raw system call primitive of
// foundation/os/Syscall.h.
//
// The argument words come from foundation/os/MemoryPolicyWords.h, which the
// door file of the primitive compares with the kernel header, and the size of
// a page comes from the auxiliary vector door.  So this file includes no
// header of the kernel and no header of the C library.

#include <fixy/os/NumaPlacement.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Errno.h>
#include <foundation/os/File.h>
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

// The policy word of mbind for a policy.  A value that no enumerator names
// gives -1, which the kernel refuses with einval.
[[nodiscard]] constexpr int policy_word(NumaPolicy policy) noexcept {
    switch (policy) {
        case NumaPolicy::bind:
            return ::foundation::os::kMemoryPolicyBind;
        case NumaPolicy::interleave:
            return ::foundation::os::kMemoryPolicyInterleave;
        case NumaPolicy::preferred:
            return ::foundation::os::kMemoryPolicyPreferred;
        default:
            return -1;
    }
}

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

// The count of the words of the node mask that the kernel reads and writes.
constexpr std::size_t numa_mask_words = numa_mask_bits / numa_mask_word_bits;

// Sets the bit of each node of NODES in the words of WORDS, which start at
// zero.  Linear in the count of the nodes.
void fill_mask_words(NumaNodeMask const& nodes, unsigned long (&words)[numa_mask_words]) noexcept {
    for (NumaNodeMask::Slot const slot : nodes.slots()) {
        std::size_t const bit = slot.raw();
        words[bit / numa_mask_word_bits] |= 1UL << (bit % numa_mask_word_bits);
    }
}

// The nodes of the bits of WORDS, or eoverflow for a bit past the nodes that
// NumaNodeId names.  Linear in the bits of the words.
[[nodiscard]] ::foundation::core::Result<NumaNodeMask, ::foundation::os::Errno>
nodes_of_mask_words(unsigned long const (&words)[numa_mask_words]) noexcept {
    NumaNodeMask nodes{};
    for (std::size_t bit = 0; bit < numa_mask_bits; ++bit) {
        if ((words[bit / numa_mask_word_bits] & (1UL << (bit % numa_mask_word_bits))) == 0) {
            continue;
        }
        if (bit >= numa_concrete_node_count) {
            return ::foundation::core::err(::foundation::os::Errno::eoverflow);
        }
        nodes.insert(static_cast<NumaNodeId>(bit)).expect("a bit below the concrete count names a concrete node");
    }
    return nodes;
}

// The policy word of set_mempolicy for a kind of a thread policy.
[[nodiscard]] constexpr int thread_policy_word(numa::ThreadPolicyKind kind) noexcept {
    switch (kind) {
        case numa::ThreadPolicyKind::first_touch:
            return ::foundation::os::kMemoryPolicyDefault;
        case numa::ThreadPolicyKind::bind:
            return ::foundation::os::kMemoryPolicyBind;
        case numa::ThreadPolicyKind::interleave:
            return ::foundation::os::kMemoryPolicyInterleave;
        case numa::ThreadPolicyKind::preferred:
            return ::foundation::os::kMemoryPolicyPreferred;
        default:
            return -1;
    }
}

}  // namespace

// get_mempolicy with no address and no flags gives the policy of the calling
// thread.  The kernel writes at most the node count that it receives, less
// one, into the mask, as mbind reads it.
::foundation::core::Result<numa::ThreadMemoryPolicy, ::foundation::os::Errno> get_mempolicy_sys() noexcept {
    int word = -1;
    unsigned long mask[numa_mask_words]{};
    for (::foundation::os::Errno const error :
         ::foundation::os::unit_of(::foundation::os::raw_syscall(::foundation::os::Syscall::get_mempolicy, &word,
                                                                 static_cast<void*>(mask), numa_mask_bits + 1,
                                                                 static_cast<void const*>(nullptr), 0))
             .err()) {
        return ::foundation::core::err(error);
    }
    numa::ThreadMemoryPolicy policy{};
    for (numa::ThreadPolicyKind const kind : numa::thread_policy_kind_of(word)) {
        policy.kind = kind;
        for (::foundation::os::Errno const error : nodes_of_mask_words(mask).err()) {
            return ::foundation::core::err(error);
        }
        policy.nodes = nodes_of_mask_words(mask).expect("the check above read the mask");
        return policy;
    }
    return ::foundation::core::err(::foundation::os::Errno::eopnotsupp);
}

// set_mempolicy reads the node count less one bits of the mask.  The policy
// first_touch takes no mask, and the door passes none.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_mempolicy_sys(numa::ThreadMemoryPolicy const& policy) noexcept {
    if (!numa::is_settable_thread_policy(policy)) {
        return ::foundation::core::err(::foundation::os::Errno::einval);
    }
    unsigned long mask[numa_mask_words]{};
    fill_mask_words(policy.nodes, mask);
    bool const has_nodes = policy.kind != numa::ThreadPolicyKind::first_touch;
    return ::foundation::os::unit_of(
        ::foundation::os::raw_syscall(::foundation::os::Syscall::set_mempolicy, thread_policy_word(policy.kind),
                                      has_nodes ? static_cast<void const*>(mask) : static_cast<void const*>(nullptr),
                                      has_nodes ? numa_mask_bits + 1 : std::size_t{0}));
}

// The kernel decrements the node count that it receives before it reads
// the mask, so the call passes one more than the width of the mask.  The
// walk of the slots of the mask is linear in the count of its words and its
// nodes.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
mbind_sys(void* address, std::size_t length, NumaPolicy policy, NumaNodeMask const& nodes) noexcept {
    if (nodes.is_empty()) {
        return ::foundation::core::err(::foundation::os::Errno::einval);
    }
    unsigned long mask[numa_mask_words]{};
    fill_mask_words(nodes, mask);
    return ::foundation::os::unit_of(::foundation::os::raw_syscall(
        ::foundation::os::Syscall::mbind, address, length, policy_word(policy), static_cast<void const*>(mask),
        numa_mask_bits + 1, ::foundation::os::kMemoryPolicyStrict | ::foundation::os::kMemoryPolicyMove));
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

// The status of a process holds about 1.5 KB, and the masks of the CPUs grow
// with the count of the CPUs that the kernel can have.  16 KB hold the
// status of a kernel of 8192 CPUs.
Result<NumaNodeMask, numa::NumaQueryError> read_allowed_numa_nodes_() noexcept {
    constexpr std::size_t status_bytes = 16384;
    FixedText<status_bytes> status{};
    for (::foundation::os::Errno const error : ::foundation::os::read_file("/proc/self/status", status).err()) {
        return err(numa::NumaQueryError{numa::NumaQueryStep::AllowedRead, ::foundation::os::error_number(error)});
    }
    return numa::allowed_numa_nodes_of_status(status.view())
        .ok_or(numa::NumaQueryError{numa::NumaQueryStep::AllowedFormat, 0});
}

}  // namespace fixy::detail
