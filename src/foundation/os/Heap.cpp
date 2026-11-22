// The bodies of the allocation door of the Ref family
// (foundation/core/Heap.h).
//
// The door calls the allocator of the C library through the builtins of
// GCC, so the file includes no system header.  Each builtin compiles to a
// call of the function of the C library with the same name, and this member
// of the archive of foundation is the one member that imports malloc,
// calloc, aligned_alloc and free (rule R5, the doors of the rule
// artifact-symbols).
// The allocator of the C library owns the state of the
// heap, so the door issues no raw system call.

#include <foundation/core/Heap.h>

#include <foundation/core/Report.h>
#include <foundation/os/Poison.h>

#include <cstddef>

namespace foundation::core::detail {

namespace {

// The alignment of each block of malloc of glibc on x86_64 and aarch64
// (MALLOC_ALIGNMENT, two words).  A larger alignment goes to
// aligned_alloc.
constexpr std::size_t kMallocAlignment = 16;

// The size of one huge page on the two targets.  A request at that
// alignment that gets no storage names the catalog entry of the remediation.
constexpr std::size_t kHugePageBytes = std::size_t{2} << 20;

[[noreturn, gnu::cold, gnu::noinline]] void refuse_zeroed_alignment_(std::size_t bytes,
                                                                     std::size_t alignment) noexcept {
    fatal("the zeroed allocation door got a request of {} bytes at the alignment {}: calloc gives at most the "
          "alignment {}.  Take a layout of a smaller alignment, and align inside the block",
          bytes, alignment, kMallocAlignment);
}

[[noreturn, gnu::cold, gnu::noinline]] void no_storage_(std::size_t bytes, std::size_t alignment) noexcept {
    if (alignment >= kHugePageBytes) {
        fatal("the allocator gave no storage for {} bytes at the alignment {}.  The catalog entry "
              "foundation::diag::HugePageAllocationFailed gives the remediation",
              bytes, alignment);
    }
    fatal("the allocator gave no storage for {} bytes at the alignment {}: the host has no memory left", bytes,
          alignment);
}

}  // namespace

// Heap.h gives the declaration the attribute returns_nonnull.  GCC 16 at -O3
// still suggests the attribute for a function that has it, also when the
// definition spells it again, so the suggestion is off for these bodies.
//
// The two words come from a Layout (the words of a layout of Heap.h), so the
// bytes are above zero and the alignment is a power of two, and no body tests
// them again.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-attribute=returns_nonnull"
void* heap_take_words_(std::size_t bytes, std::size_t alignment) noexcept {
    void* const storage =
        alignment <= kMallocAlignment ? __builtin_malloc(bytes) : __builtin_aligned_alloc(alignment, bytes);
    if (storage == nullptr) [[unlikely]] {
        no_storage_(bytes, alignment);
    }
    return storage;
}

// calloc of the C library writes zero only into a block of its own free
// list.  A block that it maps new comes from fresh pages of the kernel, which
// hold zero, so a large block costs no write.  The calloc of
// ThreadSanitizer writes zero into each block.
void* heap_take_zeroed_words_(std::size_t bytes, std::size_t alignment) noexcept {
    if (alignment > kMallocAlignment) [[unlikely]] {
        refuse_zeroed_alignment_(bytes, alignment);
    }
    void* const storage = __builtin_calloc(1, bytes);
    if (storage == nullptr) [[unlikely]] {
        no_storage_(bytes, alignment);
    }
    return storage;
}
#pragma GCC diagnostic pop

void heap_release_(void* storage, ::foundation::alloc::Layout /*layout*/) noexcept { __builtin_free(storage); }

#if defined(__SANITIZE_ADDRESS__)

// The bodies of the door of the spare storage of Heap.h, in a build with
// AddressSanitizer.
void heap_poison_(void const* first, std::size_t bytes) noexcept { ::foundation::os::poison_bytes(first, bytes); }

void heap_unpoison_(void const* first, std::size_t bytes) noexcept { ::foundation::os::unpoison_bytes(first, bytes); }

#endif

}  // namespace foundation::core::detail
